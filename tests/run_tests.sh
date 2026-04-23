#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Master test runner. Orchestrates the test suite: ioctl, file ops, error
#    paths, multi-channel DMA, /proc interface, and DMA rate tests.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Orchestrates the test suite: ioctl, file ops, error paths,
# multi-channel DMA routing, /proc interface, and DMA rate tests.
#
# Invoked by both the CI workflow and the local VM runner. Each individual
# test script is invoked sequentially; a pass/fail summary is printed at
# the end.
#
# NOTE: test_params.sh is NOT run here -- it requires module reload (sudo)
# and is invoked separately by the CI/VM harness.
#
# Environment variable contract (all have defaults, may be overridden):
#   DEV         Path to character device node (default: /dev/datadev_0)
#   APP_BIN     Directory containing test binaries
#               (default: data_dev/app/bin)
#   TESTS_DIR   Directory containing individual test_*.sh scripts
#               (default: directory containing this script)
#   DATADEV_KO  Path to datadev.ko (used by test_params.sh only)
#   CUSTOM_TX   Custom cfgTxCount (used by test_params.sh only)
#   CUSTOM_RX   Custom cfgRxCount (used by test_params.sh only)
#   CUSTOM_SIZE Custom cfgSize (used by test_params.sh only)
#
# Exit code: number of failed tests (0 = all passed).
# ----------------------------------------------------------------------------

set -uo pipefail

# Defaults
DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"
TESTS_DIR="${TESTS_DIR:-$(cd "$(dirname "$0")" && pwd)}"

# Pick a random frame size for this run (multiple of 4, range 2000..20000).
# Exported so all sub-tests use the same size, avoiding stale-frame mismatches
# when one test's leftover DMA buffers are read by the next test.
export SIZE="${SIZE:-$(( (RANDOM % 4501) * 4 + 2000 ))}"

echo "=== Test Suite ==="
echo "DEV=$DEV"
echo "APP_BIN=$APP_BIN"
echo "TESTS_DIR=$TESTS_DIR"
echo

PASSED=0
FAILED=0
FAILED_NAMES=""

# Helper: run a named test; accumulate PASS/FAIL counters.
run_test() {
   local name="$1"
   shift
   echo "--- RUN: $name ---"
   if "$@"; then
      echo "[PASS] $name"
      PASSED=$((PASSED + 1))
   else
      local rc=$?
      echo "[FAIL] $name (exit=$rc)"
      # When running under GitHub Actions, also surface as a red annotation.
      # Harmless when run locally or inside the QEMU VM (neither sets
      # GITHUB_ACTIONS), in which case the gate is closed and nothing prints.
      if [ "${GITHUB_ACTIONS:-false}" = "true" ]; then
         echo "::error title=run_tests.sh::${name} failed (exit=${rc})"
      fi
      FAILED=$((FAILED + 1))
      FAILED_NAMES="$FAILED_NAMES $name"
   fi
   echo
}

# --- Test sequence ---
run_test "ioctl_test"    "$APP_BIN/dmaIoctlTest"    -p "$DEV"
run_test "file_ops_test" "$APP_BIN/dmaFileOpsTest"  -p "$DEV"
run_test "error_paths"   "$APP_BIN/dmaErrorTest"    -p "$DEV"
run_test "multichannel"  bash "$TESTS_DIR/test_multichannel.sh"
run_test "proc_interface" bash "$TESTS_DIR/test_proc.sh"
run_test "data_integrity" bash "$TESTS_DIR/test_data_integrity.sh"
run_test "dmaLoopTest_idxEn" bash "$TESTS_DIR/test_idx_loopback.sh"
run_test "tuser_sweep"   bash "$TESTS_DIR/test_tuser_sweep.sh"
run_test "frame_sizes"   bash "$TESTS_DIR/test_frame_sizes.sh"
run_test "small_frames"  bash "$TESTS_DIR/test_small_frames.sh"
run_test "concurrent_open" bash "$TESTS_DIR/test_concurrent_open.sh"
run_test "backpressure"   bash "$TESTS_DIR/test_backpressure.sh"
run_test "irq_modes"      bash "$TESTS_DIR/test_irq_modes.sh"

# --- GPU tests (only when the GPU-enabled stack is loaded) ---
if [ "${GPU_ENABLED:-0}" = "1" ]; then
   run_test "gpu_ioctls"     "$APP_BIN/dmaGpuIoctlTest" -p "$DEV"
   run_test "gpu_proc"       bash "$TESTS_DIR/test_gpu_proc.sh"
   run_test "gpu_dma_loopback" bash "$TESTS_DIR/test_gpu_dma_loopback.sh"
fi

# --- Summary ---
echo "=== Results: $PASSED passed, $FAILED failed ==="
if [ "$FAILED" -gt 0 ]; then
   echo "Failed tests:$FAILED_NAMES"
fi

exit "$FAILED"
