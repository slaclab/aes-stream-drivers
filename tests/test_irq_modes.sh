#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    IRQ behavior sweep test. Sweeps cfgIrqHold and cfgIrqDis module
#    parameters, verifying PRBS integrity and clean dmesg for each mode.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Sweeps three IRQ-related module parameters: cfgIrqHold=1 (minimum
# coalescing), cfgIrqHold=100000 (heavy coalescing), and cfgIrqDis=1
# (polled mode). Each sub-test reloads datadev, runs dmaLoopTest for 5s,
# and verifies PRBS integrity + clean dmesg.
#
# Requires root/sudo -- this test reloads kernel modules.
#
# Environment variables:
#   DEV          Device path (default: /dev/datadev_0)
#   APP_BIN      Binary directory (default: data_dev/app/bin)
#   DATADEV_KO   Path to datadev.ko (default: data_dev/driver/datadev.ko)
#   SIZE         dmaLoopTest frame size (default: 32768)
#   TIMEOUT_SEC  Module init timeout (default: 15)
# ----------------------------------------------------------------------------

set -uo pipefail

DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"
DATADEV_KO="${DATADEV_KO:-data_dev/driver/datadev.ko}"
SIZE="${SIZE:-32768}"
TIMEOUT_SEC="${TIMEOUT_SEC:-15}"
INSMOD_TIMEOUT_SEC="${INSMOD_TIMEOUT_SEC:-120}"

# Require root/sudo capability. Skip gracefully if neither is available so
# this script can be dry-run on unprivileged hosts without breaking CI.
if [ "$(id -u)" -ne 0 ] && ! sudo -n true 2>/dev/null; then
   echo "SKIP: test_irq_modes.sh requires root/sudo (no TTY sudo allowed)"
   exit 0
fi

# sudo prefix helper -- empty when already root.
if [ "$(id -u)" -eq 0 ]; then
   SUDO=""
else
   SUDO="sudo"
fi

# Sanity check module file.
if [ ! -f "$DATADEV_KO" ]; then
   echo "FAIL: module file not found: $DATADEV_KO"
   exit 1
fi

FAILED=0

echo "=== IRQ behavior sweep test ==="

# run_irq_cycle LABEL [insmod_params...]
# Reloads datadev with the given parameters, runs a short dmaLoopTest, checks
# PRBS and dmesg. Sets CYCLE_FAIL=1 on any error.
run_irq_cycle() {
   local label="$1"
   shift
   local insmod_params="$*"

   echo "--- IRQ sub-test: $label ---"
   DMESG_BEFORE=$($SUDO dmesg | wc -l)
   CYCLE_FAIL=0

   # Unload datadev to establish known state.
   $SUDO rmmod datadev 2>/dev/null || true
   for _ in $(seq 1 15); do [ ! -e "$DEV" ] && break; sleep 0.5; done

   # Reload with IRQ params.
   # shellcheck disable=SC2086
   timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" $insmod_params

   # Wait for driver to reach live state.
   timeout $TIMEOUT_SEC bash -c "until [ \"\$(cat /sys/module/datadev/initstate 2>/dev/null)\" = live ]; do sleep 0.5; done" || {
      echo "[FAIL] irq_modes $label -- datadev did not reach live state"
      CYCLE_FAIL=1
      return
   }

   # Ensure /dev node exists (Docker containers may lack udev).
   if [ ! -e "$DEV" ]; then
      DATADEV_MAJOR=$(awk '$2 == "datadev_0" { print $1 }' /proc/devices)
      $SUDO mknod "$DEV" c "$DATADEV_MAJOR" 0
      $SUDO chmod 666 "$DEV"
   fi
   $SUDO chmod 666 "$DEV"

   sleep 2

   # Run dmaLoopTest; let it run until timeout (rc=124 ok).
   local TMPFILE
   TMPFILE=$(mktemp)
   timeout 30 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" > "$TMPFILE" 2>&1
   local LOOP_RC=$?

   # Sanity-check the dmaLoopTest exit code: 0 (clean exit) and 124
   # (timeout killed it) are both "no fatal error"; anything else means
   # the process itself crashed before a grep-able error string made it
   # to stdout.
   if [ "$LOOP_RC" -ne 0 ] && [ "$LOOP_RC" -ne 124 ]; then
      echo "[FAIL] irq_modes $label -- dmaLoopTest exited $LOOP_RC"
      CYCLE_FAIL=1
   fi

   # dmaLoopTest returns 0 even when a worker thread hit Read Error /
   # Write Error / Error opening device (see runWrite/runRead in
   # data_dev/app/src/dmaLoopTest.cpp), so $LOOP_RC alone can't
   # distinguish pass from fail — add an explicit error-string grep.
   if grep -qE "Read Error|Write Error|Error opening device" "$TMPFILE"; then
      echo "[FAIL] irq_modes $label -- dmaLoopTest worker thread reported an error"
      grep -E "Read Error|Write Error|Error opening device" "$TMPFILE" | head -5
      CYCLE_FAIL=1
   fi

   # PRBS integrity check.
   if grep -q "Prbs mismatch" "$TMPFILE"; then
      echo "[FAIL] irq_modes $label -- PRBS mismatch"
      grep "Prbs mismatch" "$TMPFILE" | head -5
      CYCLE_FAIL=1
   fi

   # NOTE: no RxCount/TxCount throughput assertion by design. The retry-once
   # wrapper (run_irq_subtest) + the dmesg oops/panic/BUG:/WARNING: scan below
   # together catch a genuinely broken IRQ mode; a brief polled-mode starvation
   # that produces few transfers is acceptable as long as the kernel is clean
   # and PRBS integrity (above) is intact.

   # Kernel error check against dmesg delta.
   DMESG_DELTA=$($SUDO dmesg | tail -n "+$((DMESG_BEFORE + 1))")
   if echo "$DMESG_DELTA" | grep -iE 'oops|panic|BUG:|WARNING:'; then
      echo "[FAIL] irq_modes $label -- kernel error in dmesg"
      CYCLE_FAIL=1
   fi

   rm -f "$TMPFILE"
}

# Sub-test 1: minimum IRQ coalescing (cfgIrqHold=1).
# Subtest runner with up to 2 attempts.  Stochastic early-frame PRBS
# mismatches have been observed on GitHub Actions runners (Azure kernel
# 6.17, under contention) even when the local KVM harness on the same
# kernel passes reliably.  Retrying catches the rare init-window race
# without masking a genuine regression: a true bug fails twice.
run_irq_subtest() {
   local label="$1"
   shift
   local params="$*"
   local attempt
   for attempt in 1 2; do
      # shellcheck disable=SC2086
      run_irq_cycle "$label" $params
      if [ "$CYCLE_FAIL" -eq 0 ]; then
         echo "[PASS] irq_modes $label"
         return 0
      fi
      if [ "$attempt" -lt 2 ]; then
         echo "[RETRY] irq_modes $label (attempt $attempt failed, retrying once)"
      fi
   done
   FAILED=$((FAILED + 1))
   return 1
}

run_irq_subtest "cfgIrqHold=1"      cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgIrqHold=1      cfgDebug=1

# Sub-test 2: heavy IRQ coalescing (cfgIrqHold=100000).
run_irq_subtest "cfgIrqHold=100000" cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgIrqHold=100000 cfgDebug=1

# Sub-test 3: polled mode (cfgIrqDis=1).
run_irq_subtest "polled"            cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgIrqDis=1      cfgDebug=1

# --- Cleanup: restore default parameters ---
$SUDO rmmod datadev 2>/dev/null || true
for _ in $(seq 1 15); do [ ! -e "$DEV" ] && break; sleep 0.5; done
timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgDebug=1 2>/dev/null || true
if [ ! -e "$DEV" ]; then
   DATADEV_MAJOR=$(awk '$2 == "datadev_0" { print $1 }' /proc/devices)
   $SUDO mknod "$DEV" c "$DATADEV_MAJOR" 0
   $SUDO chmod 666 "$DEV"
fi
$SUDO chmod 666 "$DEV" 2>/dev/null || true

exit "$FAILED"
