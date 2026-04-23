#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Load GPU modules. Loads nvidia_p2p_stub, emulator, and datadev driver
#    (GPU build) with timeout-wrapped insmod and initstate polling.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Loads emulator, nvidia_p2p_stub, and datadev driver (GPU build). Each insmod
# is wrapped in a hard timeout and each module's readiness is confirmed by
# polling /sys/module/<name>/initstate — not by grepping dmesg. Before any
# insmod, the script refuses to proceed if kernel headers for the running
# kernel are not available inside this container, and injects a unique
# baseline marker into the kernel ring buffer so check-dmesg.sh can extract
# a post-load delta. The functional assertions for GPU Async V4 registers
# and GpuAsyncCore version 4 remain — they are functional checks, not
# readiness probes.
#
# Exit codes: 0=success, 1=timeout/failure/refused
# ----------------------------------------------------------------------------

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo_step() { echo -e "${GREEN}==>${NC} $1"; }
echo_warn() { echo -e "${YELLOW}WARN:${NC} $1"; }
echo_fail() { echo -e "${RED}FAIL:${NC} $1"; }

# Detect if we need sudo
if [ "$(id -u)" -eq 0 ]; then
   SUDO=""
else
   SUDO="sudo"
fi

# Refuse to insmod when the container's kernel-headers install does
# not match the running host kernel. [ -e ] follows symlinks and returns false
# for a broken symlink — catches both "headers missing" and "headers for the
# wrong kernel revision" cases.
if [ ! -e "/lib/modules/$(uname -r)/build" ]; then
   echo_fail "Kernel headers for $(uname -r) not installed in this container — refusing to insmod"
   exit 1
fi

# Inject a unique baseline marker so check-dmesg.sh can extract
# a "since this moment" delta. Uses kernel-native sources (no package deps):
#   /proc/sys/kernel/random/uuid  — fresh v4 UUID per read, kernel ABI >= 2.6.4
#   /dev/kmsg                     — direct printk writer, requires CAP_SYS_ADMIN
#                                    (already granted by --privileged)
CI_DMESG_MARKER="BASELINE-aes-ci-$(cat /proc/sys/kernel/random/uuid)"
echo "$CI_DMESG_MARKER" | $SUDO tee /dev/kmsg > /dev/null
echo "$CI_DMESG_MARKER" > /tmp/ci_dmesg_marker
echo_step "Baseline marker injected into dmesg: $CI_DMESG_MARKER"

# Configuration
TIMEOUT_SEC="${TIMEOUT_SEC:-15}"
INSMOD_TIMEOUT_SEC="${INSMOD_TIMEOUT_SEC:-120}"
CFG_DEBUG="${CFG_DEBUG:-1}"
CFG_TX_COUNT="${CFG_TX_COUNT:-64}"
CFG_RX_COUNT="${CFG_RX_COUNT:-64}"
CFG_SIZE="${CFG_SIZE:-65536}"
CFG_MODE="${CFG_MODE:-2}"

echo_step "Loading modules (stub + emulator + GPU-enabled datadev) — insmod timeout ${INSMOD_TIMEOUT_SEC}s, initstate timeout ${TIMEOUT_SEC}s"

# --- 1) nvidia_p2p_stub ---------------------------------------------------
# MUST load before datadev_emulator — the emulator's module_init eagerly
# resolves emu_gpu_register_drain_cb (drain-callback registration is part of
# module lifecycle, not lazy). Loading emulator first yields "Unknown symbol
# emu_gpu_register_drain_cb (err -2)" and insmod fails.
timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod emulator/gpu_stub/nvidia_p2p_stub.ko || {
   rc=$?
   echo_fail "insmod nvidia_p2p_stub failed or timed out (exit $rc)"
   echo "initstate: $(cat /sys/module/nvidia_p2p_stub/initstate 2>/dev/null || echo 'missing')"
   $SUDO dmesg | tail -50
   exit 1
}

timeout $TIMEOUT_SEC bash -c "until [ \"\$(cat /sys/module/nvidia_p2p_stub/initstate 2>/dev/null)\" = live ]; do sleep 0.5; done" || {
   echo_fail "nvidia_p2p_stub initstate did not reach 'live' within ${TIMEOUT_SEC}s"
   echo "initstate: $(cat /sys/module/nvidia_p2p_stub/initstate 2>/dev/null || echo 'missing')"
   $SUDO dmesg | tail -50
   exit 1
}
echo "nvidia_p2p_stub is live"

# --- 1.5) Ensure /dev/nvidia_p2p_stub_mem exists --------------------------
# The stub registers a miscdevice (major 10) during module_init for userspace
# buffer allocation. Inside Docker, /dev is a tmpfs and udev is
# typically not running, so misc_register() populates /proc/misc but no /dev
# node appears. Create it manually from the minor number the stub registered,
# mirroring the /dev/datadev_0 pattern below. Needed by rdmaTestEmu and
# dmaGpuToggleTest, which open /dev/nvidia_p2p_stub_mem for buffer allocation.
if [ ! -e /dev/nvidia_p2p_stub_mem ]; then
   STUB_MINOR=$(awk '$2 == "nvidia_p2p_stub_mem" { print $1 }' /proc/misc)
   if [ -z "$STUB_MINOR" ]; then
      echo_fail "nvidia_p2p_stub_mem minor number not found in /proc/misc"
      $SUDO cat /proc/misc || true
      exit 1
   fi
   $SUDO mknod /dev/nvidia_p2p_stub_mem c 10 "$STUB_MINOR"
   $SUDO chmod 666 /dev/nvidia_p2p_stub_mem
   echo "Created /dev/nvidia_p2p_stub_mem (major=10 minor=$STUB_MINOR)"
fi

# --- 2) datadev_emulator --------------------------------------------------
# emu_gpu_poll_interval_us: tighten from 1000 us default to 100 us for CI.
# The GHA runner's nested-KVM scheduler routinely oversleeps usleep_range
# by multiples of the requested interval under load; the 1 ms default lets
# the poll thread stall long enough (>10 s in observed runs) that
# userspace per-doorbell deadlines trip during 10k-frame soak. 200 us was
# previously used but proved marginal — fedora:rawhide's 10k-frame soak
# would wedge after ~80 frames with rxBuffs[N]+4 doorbell timeouts. 100 us
# gives ~2x margin without pushing the emulator kthread fast enough to
# overshoot the test-side 100ms settle window (a 50 us interval exposes a
# separate race where RxFrameCnt bumps for one extra in-flight tick,
# producing rx=10001 tx=10000 at soak exit — the test's counter-equality
# check is strict and does not tolerate the documented "up to bufCnt
# extra bumps" race at runSimpleLoop cleanup).
EMU_POLL_INTERVAL_US="${EMU_POLL_INTERVAL_US:-100}"
timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod emulator/driver/datadev_emulator.ko \
   emu_gpu_poll_interval_us="$EMU_POLL_INTERVAL_US" || {
   rc=$?
   echo_fail "insmod datadev_emulator failed or timed out (exit $rc)"
   echo "initstate: $(cat /sys/module/datadev_emulator/initstate 2>/dev/null || echo 'missing')"
   $SUDO dmesg | tail -50
   exit 1
}

timeout $TIMEOUT_SEC bash -c "until [ \"\$(cat /sys/module/datadev_emulator/initstate 2>/dev/null)\" = live ]; do sleep 0.5; done" || {
   echo_fail "datadev_emulator initstate did not reach 'live' within ${TIMEOUT_SEC}s"
   echo "initstate: $(cat /sys/module/datadev_emulator/initstate 2>/dev/null || echo 'missing')"
   $SUDO dmesg | tail -50
   exit 1
}
echo "datadev_emulator is live"

# Functional assertion — GPU Async V4 BAR0 init must have happened
# during emulator init. This is NOT a readiness probe (initstate already
# guaranteed that) — it is a correctness check that the emulator built the
# expected register layout.
if ! $SUDO dmesg | grep -q "BAR0 GPU Async V4 initialized"; then
   echo_fail "GPU Async V4 init log line not found"
   $SUDO dmesg | tail -50
   exit 1
fi
echo "GPU Async V4 registers initialized"

# --- 3) datadev (GPU build) ------------------------------------------------
timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod data_dev/driver/datadev.ko cfgDebug=$CFG_DEBUG cfgTxCount=$CFG_TX_COUNT cfgRxCount=$CFG_RX_COUNT cfgSize=$CFG_SIZE cfgMode=$CFG_MODE || {
   rc=$?
   echo_fail "insmod datadev failed or timed out (exit $rc)"
   echo "initstate: $(cat /sys/module/datadev/initstate 2>/dev/null || echo 'missing')"
   $SUDO dmesg | tail -50
   exit 1
}

timeout $TIMEOUT_SEC bash -c "until [ \"\$(cat /sys/module/datadev/initstate 2>/dev/null)\" = live ]; do sleep 0.5; done" || {
   echo_fail "datadev initstate did not reach 'live' within ${TIMEOUT_SEC}s"
   echo "initstate: $(cat /sys/module/datadev/initstate 2>/dev/null || echo 'missing')"
   $SUDO dmesg | tail -50
   exit 1
}
echo "datadev is live"

# --- 3.5) Ensure /dev/datadev_0 exists ------------------------------------
# Inside Docker the container's /dev is a tmpfs, not devtmpfs, and udev is
# typically not running in the test image. device_create() in the driver
# populates /sys/class/datadev/datadev_0 but no /dev node appears.
# Create it manually from the major number the driver registered.
if [ ! -e /dev/datadev_0 ]; then
   DATADEV_MAJOR=$(awk '$2 == "datadev_0" { print $1 }' /proc/devices)
   if [ -z "$DATADEV_MAJOR" ]; then
      echo_fail "datadev major number not found in /proc/devices"
      $SUDO grep -E "^\s*[0-9]+\s+datadev" /proc/devices || true
      exit 1
   fi
   $SUDO mknod /dev/datadev_0 c "$DATADEV_MAJOR" 0
   $SUDO chmod 666 /dev/datadev_0
   echo "Created /dev/datadev_0 (major=$DATADEV_MAJOR minor=0)"
fi

# --- 4) Character device + proc entry (keep complementary probes) ---
timeout $TIMEOUT_SEC bash -c 'until [ -e /dev/datadev_0 ]; do sleep 0.5; done' || {
   echo_fail "/dev/datadev_0 not found within ${TIMEOUT_SEC}s"
   $SUDO dmesg | tail -50
   exit 1
}
echo "/dev/datadev_0 exists"

timeout $TIMEOUT_SEC bash -c 'until [ -e /proc/datadev_0 ]; do sleep 0.5; done' || {
   echo_fail "/proc/datadev_0 not found within ${TIMEOUT_SEC}s"
   $SUDO dmesg | tail -50
   exit 1
}
echo "/proc/datadev_0 exists"

# Functional assertion — GPU path was actually exercised (Gpu_Init ran
# the V4 code path against the V4 registers emulated by datadev_emulator).
if ! $SUDO dmesg | grep -q "Configured for GpuAsyncCore version 4"; then
   echo_fail "Gpu_Init V4 confirmation not found in dmesg"
   $SUDO dmesg | tail -100
   exit 1
fi
echo "Gpu_Init reported GpuAsyncCore version 4"

$SUDO cat /proc/datadev_0 > /dev/null 2>&1
sleep 2

echo_step "Modules loaded, GPU path active"
