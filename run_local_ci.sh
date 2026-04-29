#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Local CI VM runner. Boots a QEMU/TCG VM, loads kernel modules, runs
#    the full test suite, and reports pass/fail without requiring sudo.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Boots a QEMU/TCG VM, loads the nvidia_p2p_stub + datadev_emulator + datadev
# kernel modules inside the VM, runs the full Phase 3 test suite, and reports
# pass/fail. The stub is loaded first because the emulator's module_init
# directly references emu_gpu_register_drain_cb (exported by the stub) — the
# same load-order constraint enforced by scripts/ci/load-modules-cpu.sh.
#
# Designed to require NO sudo on the host. The VM boots an Ubuntu 24.04
# cloud image (cached under $CACHE_DIR), shares the host project directory
# via 9p virtfs, and executes tests/vm_inside.sh as root inside the guest.
#
# The same test scripts (tests/run_tests.sh, tests/test_params.sh) are used
# by both this local runner and the GitHub Actions CI workflow for identical
# behavior.
#
# Usage:
#   ./run_local_ci.sh
#
# Environment variable overrides (all optional):
#   VM_MEM           Guest RAM size        (default: 2G)
#   VM_CPUS          Guest vCPU count      (default: 2)
#   VM_TIMEOUT       QEMU wall-clock (s)   (default: 600)
#   CLOUD_IMG_URL    Ubuntu cloud image    (default: 24.04 amd64)
#   CACHE_DIR        Base image cache dir  (default: ~/.cache/aes-stream-local-ci)
#
# Exit code: 0 on all-pass, 1 on failure or VM error.
# ----------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# --- Configuration ---
VM_MEM="${VM_MEM:-2G}"
VM_CPUS="${VM_CPUS:-2}"
VM_TIMEOUT="${VM_TIMEOUT:-600}"
CLOUD_IMG_URL="${CLOUD_IMG_URL:-https://cloud-images.ubuntu.com/releases/24.04/release/ubuntu-24.04-server-cloudimg-amd64.img}"
CACHE_DIR="${CACHE_DIR:-$HOME/.cache/aes-stream-local-ci}"
BASE_IMG="$CACHE_DIR/ubuntu-24.04-base.img"
OVERLAY_IMG=""
SEED_ISO=""
WORK_DIR=""

cleanup() {
   if [ -n "$WORK_DIR" ] && [ -d "$WORK_DIR" ]; then
      rm -rf "$WORK_DIR"
   fi
}
trap cleanup EXIT

# --- Prerequisite checks ---
echo "=== Checking prerequisites ==="
MISSING=0
for cmd in qemu-system-x86_64 qemu-img make gcc g++; do
   if ! command -v "$cmd" >/dev/null 2>&1; then
      echo "ERROR: $cmd not found in PATH"
      MISSING=1
   fi
done

# cloud-localds from cloud-image-utils (Ubuntu/Debian). genisoimage or
# mkisofs is an acceptable fallback for building the cloud-init seed ISO.
if ! command -v cloud-localds >/dev/null 2>&1 \
      && ! command -v genisoimage >/dev/null 2>&1 \
      && ! command -v mkisofs >/dev/null 2>&1; then
   echo "ERROR: need cloud-localds (cloud-image-utils), genisoimage, or mkisofs"
   echo "  Ubuntu/Debian: sudo apt-get install cloud-image-utils"
   echo "  RHEL/Fedora:   sudo dnf install genisoimage"
   MISSING=1
fi

# curl or wget required for first-run cloud image download
if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
   echo "ERROR: need curl or wget for cloud image download"
   MISSING=1
fi

if [ "$MISSING" -ne 0 ]; then
   echo ""
   echo "Install missing tools and re-run. See README.md for setup instructions."
   exit 1
fi

# --- Build modules and tests (no sudo required) ---
echo "=== Building kernel modules and test binaries ==="
# Build gpu_stub FIRST. emulator/driver's emu_init() eagerly calls
# emu_gpu_register_drain_cb() (exported by nvidia_p2p_stub.ko), so
# kbuild needs the stub's Module.symvers to resolve cross-module refs
# at modpost, and tests/vm_inside.sh insmods the stub before the
# emulator. Mirrors the build/load order in scripts/ci/build-cpu.sh
# and scripts/ci/load-modules-cpu.sh.
make -C "$SCRIPT_DIR/emulator/gpu_stub" clean
make -C "$SCRIPT_DIR/emulator/gpu_stub"
make -C "$SCRIPT_DIR/emulator/driver" clean
make -C "$SCRIPT_DIR/emulator/driver"
make -C "$SCRIPT_DIR/data_dev/driver" clean
make -C "$SCRIPT_DIR/data_dev/driver"
make -C "$SCRIPT_DIR/data_dev/app" clean
make -C "$SCRIPT_DIR/data_dev/app"

# Verify artifacts
for f in \
   emulator/gpu_stub/nvidia_p2p_stub.ko \
   emulator/driver/datadev_emulator.ko \
   data_dev/driver/datadev.ko \
   data_dev/app/bin/dmaLoopTest \
   data_dev/app/bin/dmaRate \
   data_dev/app/bin/dmaIoctlTest \
   data_dev/app/bin/dmaFileOpsTest \
   data_dev/app/bin/dmaErrorTest; do
   if [ ! -f "$SCRIPT_DIR/$f" ]; then
      echo "ERROR: build artifact missing: $f"
      exit 1
   fi
done
echo "All build artifacts present."

# --- Prepare VM images ---
echo "=== Preparing VM images ==="
mkdir -p "$CACHE_DIR"
if [ ! -f "$BASE_IMG" ]; then
   echo "Downloading Ubuntu cloud image (one-time, ~600MB) from:"
   echo "  $CLOUD_IMG_URL"
   if command -v curl >/dev/null 2>&1; then
      curl -L -o "$BASE_IMG.tmp" "$CLOUD_IMG_URL"
   else
      wget -O "$BASE_IMG.tmp" "$CLOUD_IMG_URL"
   fi
   mv "$BASE_IMG.tmp" "$BASE_IMG"
fi

WORK_DIR=$(mktemp -d)
OVERLAY_IMG="$WORK_DIR/overlay.qcow2"
# Use a backing file so the base image is never mutated
qemu-img create -f qcow2 -b "$BASE_IMG" -F qcow2 "$OVERLAY_IMG" 20G >/dev/null

# --- Create cloud-init seed ISO ---
echo "=== Creating cloud-init seed ==="
cat > "$WORK_DIR/user-data" <<'EOF'
#cloud-config
users:
  - name: root
    lock_passwd: false
    plain_text_passwd: root
chpasswd:
  expire: false
ssh_pwauth: false
runcmd:
  - mkdir -p /mnt/host
  - mount -t 9p -o trans=virtio,version=9p2000.L host0 /mnt/host
  - bash -c 'bash /mnt/host/tests/vm_inside.sh 2>&1 | tee /mnt/host/.vm_results.log; echo "__VM_EXIT_CODE__=${PIPESTATUS[0]}" > /mnt/host/.vm_exit'
  - sync
  - poweroff
EOF

cat > "$WORK_DIR/meta-data" <<'EOF'
instance-id: aes-local-ci
local-hostname: aes-test-vm
EOF

SEED_ISO="$WORK_DIR/seed.iso"
if command -v cloud-localds >/dev/null 2>&1; then
   cloud-localds "$SEED_ISO" "$WORK_DIR/user-data" "$WORK_DIR/meta-data"
elif command -v genisoimage >/dev/null 2>&1; then
   genisoimage -output "$SEED_ISO" -volid cidata -joliet -rock \
      "$WORK_DIR/user-data" "$WORK_DIR/meta-data" >/dev/null 2>&1
else
   mkisofs -output "$SEED_ISO" -volid cidata -joliet -rock \
      "$WORK_DIR/user-data" "$WORK_DIR/meta-data" >/dev/null 2>&1
fi

# --- Clean prior result files in shared dir ---
rm -f "$SCRIPT_DIR/.vm_results.log" "$SCRIPT_DIR/.vm_exit"

# --- Boot QEMU VM ---
echo "=== Booting QEMU VM (timeout ${VM_TIMEOUT}s) ==="
echo "VM console output follows:"
echo "----------------------------------------------------------------"

# -nographic + -serial mon:stdio routes guest console to this terminal.
# -no-reboot makes `poweroff` inside the VM exit QEMU instead of rebooting.
# -virtfs shares the project root read/write so build artifacts and test
# scripts are accessible inside the guest at /mnt/host.
# No KVM -- pure TCG emulation so no sudo or /dev/kvm needed on host.
timeout "$VM_TIMEOUT" qemu-system-x86_64 \
   -m "$VM_MEM" -smp "$VM_CPUS" \
   -drive file="$OVERLAY_IMG",format=qcow2,if=virtio \
   -drive file="$SEED_ISO",format=raw,if=virtio \
   -virtfs local,path="$SCRIPT_DIR",mount_tag=host0,security_model=mapped-xattr,id=host0 \
   -nographic -no-reboot \
   -serial mon:stdio \
   </dev/null || true

echo "----------------------------------------------------------------"

# --- Capture results ---
if [ ! -f "$SCRIPT_DIR/.vm_exit" ]; then
   echo "FAIL: VM did not record exit code (boot failure or timeout?)"
   exit 1
fi

VM_RC=$(grep "^__VM_EXIT_CODE__=" "$SCRIPT_DIR/.vm_exit" | cut -d= -f2)

echo "=== VM test results (last 60 lines) ==="
if [ -f "$SCRIPT_DIR/.vm_results.log" ]; then
   tail -60 "$SCRIPT_DIR/.vm_results.log"
fi

# Clean up result artifacts so the shared dir is left tidy
rm -f "$SCRIPT_DIR/.vm_exit" "$SCRIPT_DIR/.vm_results.log"

if [ "${VM_RC:-1}" -eq 0 ]; then
   echo "=== PASS: All Phase 3 tests passed in VM ==="
   exit 0
else
   echo "=== FAIL: Phase 3 tests failed in VM (rc=$VM_RC) ==="
   exit 1
fi
