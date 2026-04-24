#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Run GPU test suite. Executes only the tests that exercise the emulated
#    GPU interface (gpu_ioctls, gpu_proc, gpu_dma_loopback) and the GPU-stack
#    load/unload cycle that validates the nvidia_p2p_stub drain_cb dependency.
#    General DMA/ioctl/proc/load-unload coverage is handled by test-cpu.sh.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Assumes modules are already loaded by load-modules-gpu.sh
#
# Exit codes: number of failed tests (0 = all passed)
# ----------------------------------------------------------------------------

set -uo pipefail

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

# Configuration
DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"
TIMEOUT_SEC="${TIMEOUT_SEC:-15}"
TESTS_DIR="${TESTS_DIR:-tests}"

# Pick a random frame size for this run (multiple of 4, range 2000..20000).
# Exported so all sub-tests use the same size, avoiding stale-frame mismatches
# between sequential invocations.
export SIZE="${SIZE:-$(( (RANDOM % 4501) * 4 + 2000 ))}"
echo_step "Using frame size: $SIZE"

FAILED=0
# LOG_FILE is a load-bearing filename: the CI workflow's diagnostic-artifact
# step copies it by literal path (.github/workflows/ci_pipeline.yml gpu_test
# diag Collect), and README.md advertises it in the post-mortem instructions.
# Keep it fixed and do NOT add a trap rm -- the next workflow step reads it.
# CI cells run in isolated Docker containers with a private /tmp, so
# concurrent-collision is not a concern.
LOG_FILE=/tmp/phase4_tests.log
: > "$LOG_FILE"

$SUDO chmod 666 "$DEV" 2>/dev/null || true

run_gpu_test() {
   local name="$1"
   shift
   echo "--- RUN: $name ---" | tee -a "$LOG_FILE"
   if "$@" 2>&1 | tee -a "$LOG_FILE"; then
      echo "[PASS] $name" | tee -a "$LOG_FILE"
   else
      local rc=${PIPESTATUS[0]}
      echo "[FAIL] $name (exit=$rc)" | tee -a "$LOG_FILE"
      if [ "${GITHUB_ACTIONS:-false}" = "true" ]; then
         echo "::error title=test-gpu.sh::${name} failed (exit=${rc})"
      fi
      FAILED=$((FAILED + 1))
   fi
   echo | tee -a "$LOG_FILE"
}

# ============================================================================
# GPU-specific tests
# ============================================================================
echo_step "Running GPU-specific tests"

run_gpu_test "gpu_ioctls"       "$APP_BIN/dmaGpuIoctlTest" -p "$DEV"
run_gpu_test "gpu_proc"         bash "$TESTS_DIR/test_gpu_proc.sh"
run_gpu_test "gpu_dma_loopback" bash "$TESTS_DIR/test_gpu_dma_loopback.sh"

if [ "$FAILED" -gt 0 ]; then
   $SUDO dmesg | tail -100
fi

# ============================================================================
# Load/unload cycles (3 cycles, GPU stack)
# ----------------------------------------------------------------------------
# Validates the GPU-stack load/unload ordering: nvidia_p2p_stub exports
# drain_cb symbols that datadev_emulator references at module-init time,
# so the stub must be loaded first and unloaded last. This is the only
# GPU-unique load/unload concern; plain datadev+emulator cycle coverage
# lives in test-cpu.sh.
# ============================================================================
echo_step "Running load/unload cycles (3 cycles, GPU stack)"
CYCLES=3

$SUDO rmmod datadev 2>/dev/null || true
$SUDO rmmod datadev_emulator 2>/dev/null || true
$SUDO rmmod nvidia_p2p_stub 2>/dev/null || true
sleep 0.5

for i in $(seq 1 $CYCLES); do
   echo "=== Cycle $i/$CYCLES ==="

   $SUDO insmod emulator/gpu_stub/nvidia_p2p_stub.ko
   $SUDO insmod emulator/driver/datadev_emulator.ko \
      emu_gpu_poll_interval_us="${EMU_POLL_INTERVAL_US:-200}"

   timeout $TIMEOUT_SEC bash -c 'until [ "$(cat /sys/module/datadev_emulator/initstate 2>/dev/null)" = live ]; do sleep 0.5; done' || {
      echo_fail "Emulator module did not initialize within ${TIMEOUT_SEC}s on cycle $i"
      $SUDO dmesg | tail -50
      FAILED=$((FAILED + 1))
      break
   }

   $SUDO insmod data_dev/driver/datadev.ko cfgTxCount=64 cfgRxCount=64 cfgSize=65536

   if [ ! -e /dev/datadev_0 ]; then
      DATADEV_MAJOR=$(awk '$2 == "datadev_0" { print $1 }' /proc/devices)
      if [ -n "$DATADEV_MAJOR" ]; then
         $SUDO mknod /dev/datadev_0 c "$DATADEV_MAJOR" 0
         $SUDO chmod 666 /dev/datadev_0
      fi
   fi

   timeout $TIMEOUT_SEC bash -c 'until [ -e /dev/datadev_0 ]; do sleep 0.5; done' || {
      echo_fail "/dev/datadev_0 not found within ${TIMEOUT_SEC}s on cycle $i"
      $SUDO dmesg | tail -50
      FAILED=$((FAILED + 1))
      break
   }
   echo "  /dev/datadev_0 exists"

   timeout $TIMEOUT_SEC bash -c 'until [ -e /proc/datadev_0 ]; do sleep 0.5; done' || {
      echo_fail "/proc/datadev_0 not found within ${TIMEOUT_SEC}s on cycle $i"
      $SUDO dmesg | tail -50
      FAILED=$((FAILED + 1))
      break
   }
   echo "  /proc/datadev_0 exists"

   $SUDO cat /proc/datadev_0 > /dev/null 2>&1
   echo "  /proc/datadev_0 readable"

   # Unload in reverse of load order: datadev -> datadev_emulator ->
   # nvidia_p2p_stub. datadev_emulator holds drain_cb symbol references
   # into nvidia_p2p_stub, so the stub must come last.
   $SUDO rmmod datadev

   timeout $TIMEOUT_SEC bash -c 'while [ -e /dev/datadev_0 ]; do sleep 0.5; done' || {
      echo_warn "/dev/datadev_0 still present after rmmod datadev on cycle $i"
   }

   $SUDO rmmod datadev_emulator
   $SUDO rmmod nvidia_p2p_stub

   sleep 0.5

   echo "  Cycle $i complete"
done

if [ $FAILED -eq 0 ]; then
   echo "All $CYCLES GPU-stack cycles passed"
fi

# ============================================================================
# Summary
# ============================================================================
if [ $FAILED -eq 0 ]; then
   echo -e "${GREEN}=== ALL GPU TESTS PASSED ===${NC}"
else
   echo -e "${RED}=== $FAILED TEST(S) FAILED ===${NC}"
fi

exit $FAILED
