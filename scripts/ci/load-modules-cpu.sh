#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Load CPU modules. Loads emulator and datadev driver with timeout-wrapped
#    insmod and initstate polling for readiness confirmation.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Loads emulator and datadev driver (CPU build). Each insmod is wrapped in a
# hard timeout and each module's readiness is confirmed by polling
# /sys/module/<name>/initstate — not by grepping dmesg. Before any
# insmod, the script refuses to proceed if kernel headers for the running
# kernel are not available inside this container, and injects a unique
# baseline marker into the kernel ring buffer so check-dmesg.sh can
# extract a post-load delta.
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

# Persist the effective RX count and buffer size for downstream test scripts
# (test-cpu.sh -> run_tests.sh -> test_proc.sh). test_proc.sh refuses to
# derive these from /proc itself (self-referential check), so callers must
# advertise what they insmod'd with via the /tmp/ci_cfg_* side-channel.
# Matches the /tmp/ci_dmesg_marker convention above.
echo "$CFG_RX_COUNT" > /tmp/ci_cfg_rx_count
echo "$CFG_SIZE"     > /tmp/ci_cfg_size

echo_step "Loading modules (CPU) — insmod timeout ${INSMOD_TIMEOUT_SEC}s, initstate timeout ${TIMEOUT_SEC}s"

# --- 1) nvidia_p2p_stub ----------------------------------------------------
# datadev_emulator.ko references emu_gpu_register_drain_cb and
# emu_gpu_unregister_drain_cb, both exported by nvidia_p2p_stub.ko. Must
# be loaded first so insmod can resolve the symbols at module load time.
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

# --- 2) datadev_emulator ---------------------------------------------------
# SIGTERM at INSMOD_TIMEOUT_SEC, SIGKILL 5s later if still alive.
# Keep emu_gpu_poll_interval_us at its 1000 us default on the CPU path:
# CPU tests have no 10k-frame soak that depends on kthread responsiveness,
# and the GHA ubuntu:24.04 runner hit a kernel swap-cgroup recursive-fault
# (lookup_swap_cgroup_id, "Fixing recursive fault but reboot is needed")
# when the tighter 200 us interval was in effect, hanging the job until
# the 20 min timeout. The GPU path still tightens to 200 us where soak
# responsiveness matters — see scripts/ci/load-modules-gpu.sh.
timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod emulator/driver/datadev_emulator.ko || {
   rc=$?
   echo_fail "insmod datadev_emulator failed or timed out (exit $rc)"
   echo "initstate: $(cat /sys/module/datadev_emulator/initstate 2>/dev/null || echo 'missing')"
   $SUDO dmesg | tail -50
   exit 1
}

# Poll /sys/module/datadev_emulator/initstate == live. Uses the exact
# timeout + until + sleep 0.5 idiom from lines 53/61 below (the /dev/datadev_0
# and /proc/datadev_0 probes). The \"\$(...)\" escaping is required inside the
# double-quoted bash -c body.
timeout $TIMEOUT_SEC bash -c "until [ \"\$(cat /sys/module/datadev_emulator/initstate 2>/dev/null)\" = live ]; do sleep 0.5; done" || {
   echo_fail "datadev_emulator initstate did not reach 'live' within ${TIMEOUT_SEC}s"
   echo "initstate: $(cat /sys/module/datadev_emulator/initstate 2>/dev/null || echo 'missing')"
   $SUDO dmesg | tail -50
   exit 1
}
echo "datadev_emulator is live"

# --- 2) datadev (CPU build) ------------------------------------------------
# Reduced buffer counts / debug logging for CI (see data_dev/driver module params).
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

# --- 2.5) Ensure /dev/datadev_0 exists --------------------------------------
# Inside Docker the container's /dev is a tmpfs, not devtmpfs, and udev is
# typically not running in the test image. device_create() in the driver
# populates /sys/class/datadev/datadev_0 but no /dev node appears.
# Create it manually from the major number the driver registered.
if [ ! -e /dev/datadev_0 ]; then
   DATADEV_MAJOR=$(awk '$2 == "datadev_0" { print $1 }' /proc/devices)
   if [ -z "$DATADEV_MAJOR" ]; then
      echo_fail "datadev major number not found in /proc/devices"
      $SUDO grep -E "^[[:space:]]*[0-9]+[[:space:]]+datadev" /proc/devices || true
      exit 1
   fi
   $SUDO mknod /dev/datadev_0 c "$DATADEV_MAJOR" 0
   $SUDO chmod 666 /dev/datadev_0
   echo "Created /dev/datadev_0 (major=$DATADEV_MAJOR minor=0)"
fi

# --- 3) Character device + proc entry (keep complementary probes) ---
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

# Read proc output (should not crash)
$SUDO cat /proc/datadev_0 > /dev/null 2>&1
echo "/proc/datadev_0 readable"

# Wait for DMA engine to capture initial buffers
sleep 2

echo_step "Modules loaded, DMA engine active"
