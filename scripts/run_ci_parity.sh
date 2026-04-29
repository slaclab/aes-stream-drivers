#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    aes-ci parity harness single entry point. Orchestrates preflight, host
#    deps, VM provisioning, hang reproduction, and CPU/GPU matrix execution.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Runs the aes-ci parity harness in six ordered stages:
#   1. preflight_kvm.sh         — verify KVM is usable + required host tools present
#   2. install-host-deps.sh     — install/advise on host packages (apt|dnf)
#   3. provision_vm.sh          — download base + overlay + seed + virt-install + verify
#   4. hang_repro.sh            — drive the ubuntu:24.04 insmod hang inside the guest
#                                 (skipped if --no-hang-repro)
#   5. run_matrix.sh --phase cpu — execute the CPU matrix (gated by --matrix)
#   6. run_matrix.sh --phase gpu — execute the GPU matrix (gated by --matrix;
#                                   runs after CPU matrix completes)
#
# This script holds NO business logic of its own — it dispatches to the six
# stage scripts under scripts/ci-local/. Exit codes from the first failing
# stage propagate to the shell.
#
# Usage:
#   ./scripts/run_ci_parity.sh                   # provision + hang-repro
#   ./scripts/run_ci_parity.sh --reset           # re-provision from scratch
#   ./scripts/run_ci_parity.sh --clean           # tear down VM and exit
#   ./scripts/run_ci_parity.sh --no-hang-repro   # provision only
#   ./scripts/run_ci_parity.sh --matrix          # provision + hang-repro + CPU + GPU matrix
#   ./scripts/run_ci_parity.sh -h                # usage message
#
# Environment variables (passed through to provision_vm.sh):
#   AES_CI_CACHE_DIR, AES_CI_BASE_IMAGE_URL, AES_CI_VM_MEMORY, AES_CI_VM_VCPUS,
#   AES_CI_DOMAIN
#
# Exit codes:
#   0   = all stages passed
#   1-4 = exit code from preflight_kvm.sh
#   10  = install-host-deps.sh failed
#   20-25 = exit code from provision_vm.sh offset by 20 (so caller can tell
#            provisioning failures apart from other stages)
#   30  = hang_repro.sh reproduced a hang (diagnostics captured)
#   40+ = exit code from run_matrix.sh --phase cpu offset by 40 (cell failure
#         aggregate; typically 41 for one or more cells failed)
#   50+ = exit code from run_matrix.sh --phase gpu offset by 50
#   5   = unsupported flag / usage error
# ----------------------------------------------------------------------------

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CI_LOCAL_DIR="$SCRIPT_DIR/ci-local"

# shellcheck source=ci-local/lib/common.sh
source "$SCRIPT_DIR/ci-local/lib/common.sh"

# ----------------------------------------------------------------------------
# Flag parsing
# ----------------------------------------------------------------------------
RESET_FLAG=""
CLEAN_FLAG=""
SKIP_HANG_REPRO=0
RUN_MATRIX=0

usage() {
   cat <<EOF
Usage: $0 [OPTIONS]

Provisions the aes-ci parity VM and runs the full CI matrix locally.

OPTIONS:
   --reset           Tear down existing VM and re-provision from scratch
   --clean           Tear down existing VM and exit (no re-provision)
   --no-hang-repro   Provision only; skip the hang-reproduction stage
   --matrix          After provisioning + hang-repro (unless --no-hang-repro),
                     execute both CPU and GPU matrix phases via
                     scripts/ci-local/run_matrix.sh (mirrors the GitHub
                     workflow's CPU and GPU test phases).
   -h, --help        Show this message and exit

ENVIRONMENT:
   AES_CI_CACHE_DIR       Override cache dir (default: \$HOME/.cache/aes-ci-parity)
   AES_CI_BASE_IMAGE_URL  Override base cloud image URL
   AES_CI_VM_MEMORY       Guest RAM in MB (default: 4096)
   AES_CI_VM_VCPUS        Guest vCPU count (default: 4)
   AES_CI_DOMAIN          libvirt domain name (default: aes-ci)

EXAMPLES:
   $0                     # Provision if needed, then reproduce hang
   $0 --reset             # Force re-provision
   $0 --no-hang-repro     # Just provision; don't run the hang repro
   AES_CI_VM_MEMORY=8192 $0   # Provision with 8 GB guest RAM
   $0 --matrix            # Provision + hang-repro + CPU matrix + GPU matrix

See scripts/ci-local/README.md for prerequisites and troubleshooting.
EOF
}

while [ $# -gt 0 ]; do
   case "$1" in
      --reset)          RESET_FLAG="--reset" ;;
      --clean)          CLEAN_FLAG="--clean" ;;
      --no-hang-repro)  SKIP_HANG_REPRO=1 ;;
      --matrix)         RUN_MATRIX=1 ;;
      -h|--help)        usage; exit 0 ;;
      *)
         echo_fail "Unknown flag: $1"
         usage
         exit 5
         ;;
   esac
   shift
done

# ----------------------------------------------------------------------------
# Orchestration — 6 stages
# ----------------------------------------------------------------------------
echo_header "aes-ci parity harness"

# --- Stage 1 / 6: preflight -----------------------------------------------
echo_header "Stage 1 / 6: host preflight"
RC=0
bash "$CI_LOCAL_DIR/preflight_kvm.sh" || RC=$?
if [ $RC -ne 0 ]; then
   echo_fail "Preflight failed (exit $RC). See scripts/ci-local/README.md Troubleshooting."
   exit "$RC"
fi

# --- Stage 2 / 6: host dependency install --------------------------------
echo_header "Stage 2 / 6: host dependency install"
if ! bash "$CI_LOCAL_DIR/install-host-deps.sh"; then
   echo_fail "Host dependency install failed."
   exit 10
fi
# Note: as a non-root user, install-host-deps.sh exits 0 after PRINTING the
# install commands (we never elevate silently). That's fine for this
# orchestrator — preflight's tool-presence check will fail on the NEXT
# invocation if the user ignored the copy-paste output. No loop here.

# --- Stage 3 / 6: VM provisioning -----------------------------------------
echo_header "Stage 3 / 6: VM provisioning"
PROVISION_ARGS=()
[ -n "$RESET_FLAG" ] && PROVISION_ARGS+=("$RESET_FLAG")
[ -n "$CLEAN_FLAG" ] && PROVISION_ARGS+=("$CLEAN_FLAG")

RC=0
bash "$CI_LOCAL_DIR/provision_vm.sh" "${PROVISION_ARGS[@]}" || RC=$?
if [ $RC -ne 0 ]; then
   echo_fail "VM provisioning failed (exit $RC)."
   exit $((RC + 20))
fi

# --clean exits early inside provision_vm.sh with exit 0 after teardown —
# no VM exists, so hang-repro cannot run. Honor that.
if [ -n "$CLEAN_FLAG" ]; then
   echo_step "Clean teardown complete; no VM provisioned, nothing more to do."
   exit 0
fi

# ----------------------------------------------------------------------------
# Human-verify checkpoint (post-provisioning)
# ----------------------------------------------------------------------------
echo_header "Human-verify checkpoint (post-provisioning)"
cat <<EOF
Before proceeding to Stage 4 (hang reproduction), verify the VM manually:

   # Find the guest IP
   virsh domifaddr --source agent \${AES_CI_DOMAIN:-aes-ci}

   # SSH into the guest and confirm
   ssh -i \${AES_CI_CACHE_DIR:-\$HOME/.cache/aes-ci-parity}/id_ed25519 ubuntu@<IP>

   # Expected checks:
   #   uname -r                               should contain 'azure'
   #   docker version --format '{{.Server.Version}}'   should start with '28.'
   #   ls /var/lib/aes-ci-cloud-init-done     should exist
   #   sudo cat /proc/sys/kernel/sysrq        should report 1

If any check fails, run:   $0 --reset

EOF

# --- Stage 4 / 6: hang reproduction ---------------------------------------
if [ "$SKIP_HANG_REPRO" -eq 1 ]; then
   echo_step "--no-hang-repro set; skipping Stage 4 (hang reproduction)."
   if [ "$RUN_MATRIX" -ne 1 ]; then
      echo_step "VM is running and ready. Entry point exits 0."
      exit 0
   fi
   echo_step "Continuing to Stage 5 (--matrix set)."
else
   echo_header "Stage 4 / 6: hang reproduction"
   if [ ! -x "$CI_LOCAL_DIR/hang_repro.sh" ] && [ ! -f "$CI_LOCAL_DIR/hang_repro.sh" ]; then
      echo_warn "hang_repro.sh not present. Skipping."
      echo_step "Entry point exits 0 (VM is ready, hang-repro deferred)."
      exit 0
   fi

   if ! bash "$CI_LOCAL_DIR/hang_repro.sh"; then
      echo_fail "Hang reproduced — diagnostics captured."
      echo_fail "  See .diag/ for sysrq-t, dmesg, /proc/<pid>/stack."
      exit 30
   fi

   echo_header "Stages 1-4 passed"
   echo_step "VM running, Azure kernel + Docker 28.x verified, no hang reproduced."
fi

# --- Stage 5 / 6: CPU matrix execution ------------------------------------
if [ "$RUN_MATRIX" -ne 1 ]; then
   echo_step "--matrix not set; skipping Stages 5-6 (CPU + GPU matrix execution)."
   echo_step "VM is ready, hang-repro passed. Entry point exits 0."
   echo_step "Re-run with --matrix to execute the full CPU + GPU matrix."
   exit 0
fi

echo_header "Stage 5 / 6: CPU matrix execution"
RC=0
bash "$CI_LOCAL_DIR/run_matrix.sh" --phase cpu || RC=$?
if [ $RC -ne 0 ]; then
   echo_fail "CPU matrix execution failed (aggregate exit $RC)."
   echo_fail "  See logs/<ts>/<sanitized>/*.log for per-cell output."
   exit $((RC + 40))
fi
echo_step "CPU matrix complete; continuing to GPU matrix."

# --- Stage 6 / 6: GPU matrix execution ------------------------------------
echo_header "Stage 6 / 6: GPU matrix execution"
RC=0
bash "$CI_LOCAL_DIR/run_matrix.sh" --phase gpu || RC=$?
if [ $RC -ne 0 ]; then
   echo_fail "GPU matrix execution failed (aggregate exit $RC)."
   echo_fail "  See logs/<ts>/<sanitized>/*.log for per-cell output."
   exit $((RC + 50))
fi

echo_header "All stages passed"
echo_step "VM running, CPU + GPU matrix complete — see logs/<ts>/ for per-cell output."
exit 0
