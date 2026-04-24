#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Run CPU test suite. Executes DMA loopback, test suite, module parameter
#    validation, and load/unload cycles.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Runs all CPU tests: dmaLoopTest, test suite, module parameter
# validation, and 3 load/unload cycles.
#
# Assumes modules are already loaded by load-modules-cpu.sh
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
INSMOD_TIMEOUT_SEC="${INSMOD_TIMEOUT_SEC:-120}"
export INSMOD_TIMEOUT_SEC
TESTS_DIR="${TESTS_DIR:-tests}"

# Pick a random frame size for this run (multiple of 4, range 2000..20000).
# Exported so all sub-tests (run_tests.sh, etc.) use the same size, avoiding
# stale-frame mismatches between sequential dmaLoopTest invocations.
export SIZE="${SIZE:-$(( (RANDOM % 4501) * 4 + 2000 ))}"
echo_step "Using frame size: $SIZE"

FAILED=0

# PHASE3_LOG / PARAMS_LOG are load-bearing filenames: the CI workflow's
# diagnostic-artifact step copies them by literal path
# (.github/workflows/ci_pipeline.yml cpu_test/diag Collect), and README.md
# advertises them in the post-mortem instructions. Keep them fixed and do
# NOT add a trap rm -- the next workflow step needs to read these files.
# CI cells run in isolated Docker containers with a private /tmp, so
# concurrent-collision is not a concern.
PHASE3_LOG=/tmp/phase3_tests.log
PARAMS_LOG=/tmp/test_params.log
# GAP13_LOOP_LOG is internal-only (no workflow reference); mktemp + trap
# so reruns on a shared host don't reuse stale state.
GAP13_LOOP_LOG=$(mktemp -t gap13_loop.XXXXXX)
trap 'rm -f "$GAP13_LOOP_LOG"' EXIT

# ============================================================================
# Test 1: DMA loopback test
# ============================================================================
echo_step "Running DMA loopback test"
$SUDO chmod 666 $DEV

LOG_FILE=dma_loop_output.txt
echo "Running dmaLoopTest (30s, dest 0, size $SIZE) -- output captured to $LOG_FILE (dumped only on failure)"
timeout 30 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" > "$LOG_FILE" 2>&1
LOOP_RC=$?

LOG_LINES=$(wc -l < "$LOG_FILE" 2>/dev/null || echo 0)
if [ "$LOOP_RC" -eq 124 ]; then
   echo "dmaLoopTest ran for full 30s with no errors (timeout is success); ${LOG_LINES} lines logged"
elif [ "$LOOP_RC" -eq 0 ]; then
   echo "dmaLoopTest exited cleanly; ${LOG_LINES} lines logged"
else
   echo_fail "dmaLoopTest exited with code $LOOP_RC"
   echo "--- begin $LOG_FILE ---"
   cat "$LOG_FILE"
   echo "--- end $LOG_FILE ---"
   $SUDO dmesg | tail -100
   FAILED=$((FAILED + 1))
fi

# dmaLoopTest returns 0 even when a worker thread hits Read Error /
# Write Error / Error opening device (runWrite/runRead flip running=false
# and main() returns 0 — see data_dev/app/src/dmaLoopTest.cpp), so
# LOOP_RC alone can't distinguish pass from fail. Match the grep guards
# the tests/ cells use for the same binary.
if grep -qE "Read Error|Write Error|Error opening device|Prbs mismatch" "$LOG_FILE"; then
   echo_fail "dmaLoopTest worker thread reported an error"
   grep -E "Read Error|Write Error|Error opening device|Prbs mismatch" "$LOG_FILE" | head -5
   $SUDO dmesg | tail -100
   FAILED=$((FAILED + 1))
fi

# Verify some data was actually transferred
if grep -q "TxCount:" "$LOG_FILE"; then
   echo "DMA loopback test produced transfer statistics"
else
   echo_warn "No transfer statistics found in output"
fi

# Allow DMA engine to drain in-flight buffers before running unit tests.
# The emulator processes one TX per ~10ms IRQ cycle; with 64 TX buffers
# potentially queued, a 2s pause ensures all buffers return to the pool.
sleep 2

# ============================================================================
# Test 3: Test suite
# ============================================================================
echo_step "Running test suite"
# Forward the RX count / buffer size that load-modules-cpu.sh (or -gpu.sh)
# actually insmod'd so test_proc.sh can assert /proc matches them. Without
# this forwarding, test_proc.sh falls back to driver compile-time defaults
# (1024/131072) which no longer match the reduced CI values (64/65536).
if [ -r /tmp/ci_cfg_rx_count ]; then
   export EXPECTED_BUFF_COUNT="$(cat /tmp/ci_cfg_rx_count)"
fi
if [ -r /tmp/ci_cfg_size ]; then
   export EXPECTED_BUFF_SIZE="$(cat /tmp/ci_cfg_size)"
fi
bash $TESTS_DIR/run_tests.sh 2>&1 | tee "$PHASE3_LOG"
PHASE3_RC=${PIPESTATUS[0]}

# Display summary
TOTAL_PASS=$(grep -c '^\[PASS\]' "$PHASE3_LOG" || true)
TOTAL_FAIL=$(grep -c '^\[FAIL\]' "$PHASE3_LOG" || true)
echo "Test Summary:"
echo "  PASS: ${TOTAL_PASS:-0}"
echo "  FAIL: ${TOTAL_FAIL:-0}"

if [ "$PHASE3_RC" -ne 0 ]; then
   echo_fail "${PHASE3_RC} test(s) failed"
   grep '^\[FAIL\]' "$PHASE3_LOG" || true
   $SUDO dmesg | tail -100
   FAILED=$((FAILED + PHASE3_RC))
fi

# ============================================================================
# Test 4: Module parameter validation test
# ============================================================================
echo_step "Running module parameter validation test"
DEV=$DEV \
APP_BIN=$APP_BIN \
DATADEV_KO=data_dev/driver/datadev.ko \
CUSTOM_TX=256 \
CUSTOM_RX=256 \
CUSTOM_SIZE=65536 \
  bash $TESTS_DIR/test_params.sh 2>&1 | tee "$PARAMS_LOG"
PARAMS_RC=${PIPESTATUS[0]}

TOTAL_PASS=$(grep -c '^\[PASS\]' "$PARAMS_LOG" || true)
TOTAL_FAIL=$(grep -c '^\[FAIL\]' "$PARAMS_LOG" || true)
echo "Module Parameter Validation Summary:"
echo "  PASS: ${TOTAL_PASS:-0}"
echo "  FAIL: ${TOTAL_FAIL:-0}"

if [ "$PARAMS_RC" -ne 0 ]; then
   echo_fail "${PARAMS_RC} param check(s) failed"
   grep '^\[FAIL\]' "$PARAMS_LOG" || true
   $SUDO dmesg | tail -100
   FAILED=$((FAILED + PARAMS_RC))
fi

# ============================================================================
# Test 5: Load/unload cycles (3 cycles)
# ============================================================================
echo_step "Running load/unload cycles (3 cycles)"
CYCLES=3

# Ensure modules are unloaded before starting cycles (test_params.sh may
# have left them loaded).
$SUDO rmmod datadev 2>/dev/null || true
$SUDO rmmod datadev_emulator 2>/dev/null || true
sleep 0.5

DATADEV_KO=data_dev/driver/datadev.ko

# ============================================================
# cfgMode=2 (BUFF_STREAM) reload test
# ============================================================
echo_step "cfgMode=2 (BUFF_STREAM) reload test"
GAP1_FAILED=0
DMESG_BEFORE=$($SUDO dmesg | wc -l)

timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod emulator/driver/datadev_emulator.ko || {
   echo_fail "insmod emulator failed or timed out"
   FAILED=$((FAILED + 1))
   GAP1_FAILED=1
}
timeout $TIMEOUT_SEC bash -c 'until [ "$(cat /sys/module/datadev_emulator/initstate 2>/dev/null)" = live ]; do sleep 0.5; done' || {
   echo_fail "emulator did not initialize within ${TIMEOUT_SEC}s"
   FAILED=$((FAILED + 1))
   GAP1_FAILED=1
}

if [ "$GAP1_FAILED" -eq 0 ]; then
   timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" cfgMode=2 cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgDebug=1
   timeout $TIMEOUT_SEC bash -c "until [ \"\$(cat /sys/module/datadev/initstate 2>/dev/null)\" = live ]; do sleep 0.5; done" || {
      echo_fail "datadev did not initialize within ${TIMEOUT_SEC}s"
      FAILED=$((FAILED + 1))
      GAP1_FAILED=1
   }
fi

if [ "$GAP1_FAILED" -eq 0 ]; then
   if [ ! -e "$DEV" ]; then
      DATADEV_MAJOR=$(awk '$2 == "datadev_0" { print $1 }' /proc/devices)
      $SUDO mknod "$DEV" c "$DATADEV_MAJOR" 0
      $SUDO chmod 666 "$DEV"
   fi
   $SUDO chmod 666 "$DEV"
   sleep 2

   bash "$TESTS_DIR/test_data_integrity.sh"
   GAP1_INTG_RC=$?

   DMESG_DELTA=$($SUDO dmesg | tail -n "+$((DMESG_BEFORE + 1))")
   GAP1_DMESG_ERRORS=$(echo "$DMESG_DELTA" | grep -iE 'oops|panic|BUG:|WARNING:' || true)

   if [ "$GAP1_INTG_RC" -eq 0 ] && \
      [ -z "$GAP1_DMESG_ERRORS" ]; then
      echo "[PASS] data_integrity (cfgMode=2)"
   else
      echo "[FAIL] data_integrity (cfgMode=2)"
      FAILED=$((FAILED + 1))
   fi

   # Restore cfgMode=1 before cleanup
   $SUDO rmmod datadev 2>/dev/null || true
   for _ in $(seq 1 15); do [ ! -e "$DEV" ] && break; sleep 0.5; done
   timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" cfgMode=1 cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgDebug=1
   timeout $TIMEOUT_SEC bash -c "until [ \"\$(cat /sys/module/datadev/initstate 2>/dev/null)\" = live ]; do sleep 0.5; done" || true
   if [ ! -e "$DEV" ]; then
      DATADEV_MAJOR=$(awk '$2 == "datadev_0" { print $1 }' /proc/devices)
      $SUDO mknod "$DEV" c "$DATADEV_MAJOR" 0
      $SUDO chmod 666 "$DEV"
   fi
fi

# Cleanup
$SUDO rmmod datadev 2>/dev/null || true
$SUDO rmmod datadev_emulator 2>/dev/null || true
sleep 0.5

# ============================================================
# rmmod-under-load test
# ============================================================
echo_step "rmmod-under-load test"
GAP5_FAILED=0
DMESG_BEFORE=$($SUDO dmesg | wc -l)

timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod emulator/driver/datadev_emulator.ko || {
   echo_fail "insmod emulator failed or timed out"
   FAILED=$((FAILED + 1))
   GAP5_FAILED=1
}
timeout $TIMEOUT_SEC bash -c 'until [ "$(cat /sys/module/datadev_emulator/initstate 2>/dev/null)" = live ]; do sleep 0.5; done' || {
   echo_fail "emulator did not initialize within ${TIMEOUT_SEC}s"
   FAILED=$((FAILED + 1))
   GAP5_FAILED=1
}

if [ "$GAP5_FAILED" -eq 0 ]; then
   timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgDebug=1
   timeout $TIMEOUT_SEC bash -c "until [ \"\$(cat /sys/module/datadev/initstate 2>/dev/null)\" = live ]; do sleep 0.5; done" || {
      echo_fail "datadev did not initialize within ${TIMEOUT_SEC}s"
      FAILED=$((FAILED + 1))
      GAP5_FAILED=1
   }
fi

if [ "$GAP5_FAILED" -eq 0 ]; then
   if [ ! -e "$DEV" ]; then
      DATADEV_MAJOR=$(awk '$2 == "datadev_0" { print $1 }' /proc/devices)
      $SUDO mknod "$DEV" c "$DATADEV_MAJOR" 0
      $SUDO chmod 666 "$DEV"
   fi
   $SUDO chmod 666 "$DEV"
   sleep 2

   "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" &
   LOOP_PID=$!
   sleep 3

   kill $LOOP_PID 2>/dev/null || true
   wait $LOOP_PID 2>/dev/null || true

   timeout 30 $SUDO rmmod datadev || { echo_fail "rmmod datadev failed/timed-out after kill"; FAILED=$((FAILED + 1)); GAP5_FAILED=1; }
   for _ in $(seq 1 15); do [ ! -e "$DEV" ] && break; sleep 0.5; done

   timeout 30 $SUDO rmmod datadev_emulator || { echo_fail "rmmod datadev_emulator failed/timed-out"; FAILED=$((FAILED + 1)); GAP5_FAILED=1; }

   DMESG_DELTA=$($SUDO dmesg | tail -n "+$((DMESG_BEFORE + 1))")
   GAP5_DMESG_ERRORS=$(echo "$DMESG_DELTA" | grep -iE 'oops|panic|BUG:|WARNING:' || true)

   if [ "$GAP5_FAILED" -eq 0 ] && [ -z "$GAP5_DMESG_ERRORS" ]; then
      echo "[PASS] rmmod-under-load"
   else
      echo "[FAIL] rmmod-under-load"
      FAILED=$((FAILED + 1))
   fi
fi

sleep 0.5

# If the kernel oopsed during rmmod-under-load, remaining insmod/rmmod calls
# will hang forever.  Bail out early so CI doesn't hit the job timeout.
# Scan only the post-marker delta (driver-induced) so boot-time messages or
# messages from a prior cell cannot spuriously trip the gate. Marker is
# injected by scripts/ci/load-modules-cpu.sh into /tmp/ci_dmesg_marker.
KERNEL_HEALTH_MARKER=""
if [ -r /tmp/ci_dmesg_marker ]; then
   KERNEL_HEALTH_MARKER="$(cat /tmp/ci_dmesg_marker)"
fi
if [ -n "$KERNEL_HEALTH_MARKER" ]; then
   KERNEL_HEALTH=$($SUDO dmesg | awk -v m="$KERNEL_HEALTH_MARKER" 'f{print} index($0,m){f=1}' | \
                   grep -ciE 'oops|panic|BUG:|reboot is needed' || true)
else
   # Fallback: no marker available (should not happen when load-modules-cpu.sh
   # ran first). Use full-ring scan as a last-resort health signal.
   KERNEL_HEALTH=$($SUDO dmesg | grep -ciE 'oops|panic|BUG:|reboot is needed' || true)
fi
if [ "$KERNEL_HEALTH" -gt 0 ]; then
   echo_fail "Kernel oops/panic detected after rmmod-under-load — skipping remaining tests"
   FAILED=$((FAILED + 1))
   exit $FAILED
fi

# ============================================================
# cfgBgThold per-channel threshold reload test
# ============================================================
echo_step "cfgBgThold0=16 cfgBgThold1=8 reload test"
GAP13_FAILED=0
DMESG_BEFORE=$($SUDO dmesg | wc -l)

timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod emulator/driver/datadev_emulator.ko || {
   echo_fail "insmod emulator failed or timed out"
   FAILED=$((FAILED + 1))
   GAP13_FAILED=1
}
timeout $TIMEOUT_SEC bash -c 'until [ "$(cat /sys/module/datadev_emulator/initstate 2>/dev/null)" = live ]; do sleep 0.5; done' || {
   echo_fail "emulator did not initialize within ${TIMEOUT_SEC}s"
   FAILED=$((FAILED + 1))
   GAP13_FAILED=1
}

if [ "$GAP13_FAILED" -eq 0 ]; then
   timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" cfgBgThold0=16 cfgBgThold1=8 cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgDebug=1
   timeout $TIMEOUT_SEC bash -c "until [ \"\$(cat /sys/module/datadev/initstate 2>/dev/null)\" = live ]; do sleep 0.5; done" || {
      echo_fail "datadev did not initialize within ${TIMEOUT_SEC}s"
      FAILED=$((FAILED + 1))
      GAP13_FAILED=1
   }
fi

if [ "$GAP13_FAILED" -eq 0 ]; then
   if [ ! -e "$DEV" ]; then
      DATADEV_MAJOR=$(awk '$2 == "datadev_0" { print $1 }' /proc/devices)
      $SUDO mknod "$DEV" c "$DATADEV_MAJOR" 0
      $SUDO chmod 666 "$DEV"
   fi
   $SUDO chmod 666 "$DEV"
   sleep 2

   timeout 30 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0,1 -s "$SIZE" > "$GAP13_LOOP_LOG" 2>&1
   GAP13_LOOP_RC=$?

   DMESG_DELTA=$($SUDO dmesg | tail -n "+$((DMESG_BEFORE + 1))")
   GAP13_DMESG_ERRORS=$(echo "$DMESG_DELTA" | grep -iE 'oops|panic|BUG:|WARNING:' || true)
   # Explicit if/else avoids the `grep -q ... && echo X || true` idiom: in a
   # `A && B || C` chain, C still fires when A succeeds but B fails, which can
   # silently flip a PASS into a FAIL on an unrelated failure in B.
   if grep -q "Prbs mismatch" "$GAP13_LOOP_LOG"; then
      GAP13_PRBS_ERRORS="mismatch"
   else
      GAP13_PRBS_ERRORS=""
   fi

   if [ -z "$GAP13_PRBS_ERRORS" ] && [ -z "$GAP13_DMESG_ERRORS" ] && \
      { [ "$GAP13_LOOP_RC" -eq 0 ] || [ "$GAP13_LOOP_RC" -eq 124 ]; }; then
      echo "[PASS] bgthold"
   else
      echo "[FAIL] bgthold"
      FAILED=$((FAILED + 1))
   fi
fi

# Cleanup
$SUDO rmmod datadev 2>/dev/null || true
$SUDO rmmod datadev_emulator 2>/dev/null || true
sleep 0.5

for i in $(seq 1 $CYCLES); do
   echo "=== Cycle $i/$CYCLES ==="

   # Load emulator first
   timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod emulator/driver/datadev_emulator.ko || {
      echo_fail "insmod emulator failed or timed out on cycle $i"
      $SUDO dmesg | tail -50
      FAILED=$((FAILED + 1))
      break
   }

   # Wait for emulator module to reach 'live' initstate (robust alternative
   # to grepping dmesg which is fragile on busy kernels).
   timeout $TIMEOUT_SEC bash -c 'until [ "$(cat /sys/module/datadev_emulator/initstate 2>/dev/null)" = live ]; do sleep 0.5; done' || {
      echo_fail "Emulator module did not initialize within ${TIMEOUT_SEC}s on cycle $i"
      $SUDO dmesg | tail -50
      FAILED=$((FAILED + 1))
      break
   }

   # Load datadev driver with reduced buffer counts for CI
   timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod data_dev/driver/datadev.ko cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgMode=2

   # Wait for datadev module to reach 'live' initstate
   timeout $TIMEOUT_SEC bash -c 'until [ "$(cat /sys/module/datadev/initstate 2>/dev/null)" = live ]; do sleep 0.5; done' || {
      echo_fail "datadev module did not initialize within ${TIMEOUT_SEC}s on cycle $i"
      $SUDO dmesg | tail -50
      FAILED=$((FAILED + 1))
      break
   }

   # Create device node if udev is not available (Docker/container environments).
   if [ ! -e /dev/datadev_0 ]; then
      DATADEV_MAJOR=$(awk '$2 == "datadev_0" { print $1 }' /proc/devices)
      $SUDO mknod /dev/datadev_0 c "$DATADEV_MAJOR" 0
      $SUDO chmod 666 /dev/datadev_0
   fi
   echo "  /dev/datadev_0 exists"

   # Verify proc entry
   timeout $TIMEOUT_SEC bash -c 'until [ -e /proc/datadev_0 ]; do sleep 0.5; done' || {
      echo_fail "/proc/datadev_0 not found within ${TIMEOUT_SEC}s on cycle $i"
      $SUDO dmesg | tail -50
      FAILED=$((FAILED + 1))
      break
   }
   echo "  /proc/datadev_0 exists"

   # Read proc output (should not crash)
   $SUDO cat /proc/datadev_0 > /dev/null 2>&1
   echo "  /proc/datadev_0 readable"

   # Unload in reverse order
   $SUDO rmmod datadev

   # Remove device node (udev won't clean it up in Docker/container environments).
   $SUDO rm -f /dev/datadev_0

   # Wait for datadev module to fully unload before removing emulator.
   timeout $TIMEOUT_SEC bash -c 'while [ -e /sys/module/datadev ]; do sleep 0.5; done' || {
      echo_warn "datadev module still present after rmmod on cycle $i"
   }

   $SUDO rmmod datadev_emulator

   # Brief pause between cycles for kernel to settle
   sleep 0.5

   echo "  Cycle $i complete"
done

if [ $FAILED -eq 0 ]; then
   echo "All $CYCLES cycles passed"
fi

# ============================================================================
# Summary
# ============================================================================
if [ $FAILED -eq 0 ]; then
   echo -e "${GREEN}=== ALL CPU TESTS PASSED ===${NC}"
else
   echo -e "${RED}=== $FAILED TEST(S) FAILED ===${NC}"
fi

exit $FAILED
