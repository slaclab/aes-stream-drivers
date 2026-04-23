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

   # PRBS integrity check.
   if grep -q "Prbs mismatch" "$TMPFILE"; then
      echo "[FAIL] irq_modes $label -- PRBS mismatch"
      grep "Prbs mismatch" "$TMPFILE" | head -5
      CYCLE_FAIL=1
   fi

   # Kernel error check against dmesg delta.
   DMESG_DELTA=$($SUDO dmesg | tail -n "+$((DMESG_BEFORE + 1))")
   if echo "$DMESG_DELTA" | grep -iE 'oops|panic|BUG:|WARNING:'; then
      echo "[FAIL] irq_modes $label -- kernel error in dmesg"
      CYCLE_FAIL=1
   fi

   rm -f "$TMPFILE"
}

# Sub-test 1: minimum IRQ coalescing (cfgIrqHold=1).
run_irq_cycle "cfgIrqHold=1" cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgIrqHold=1 cfgDebug=1
if [ "$CYCLE_FAIL" -eq 0 ]; then
   echo "[PASS] irq_modes cfgIrqHold=1"
else
   FAILED=$((FAILED + CYCLE_FAIL))
fi

# Sub-test 2: heavy IRQ coalescing (cfgIrqHold=100000).
run_irq_cycle "cfgIrqHold=100000" cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgIrqHold=100000 cfgDebug=1
if [ "$CYCLE_FAIL" -eq 0 ]; then
   echo "[PASS] irq_modes cfgIrqHold=100000"
else
   FAILED=$((FAILED + CYCLE_FAIL))
fi

# Sub-test 3: polled mode (cfgIrqDis=1).
run_irq_cycle "polled" cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgIrqDis=1 cfgDebug=1
if [ "$CYCLE_FAIL" -eq 0 ]; then
   echo "[PASS] irq_modes polled"
else
   FAILED=$((FAILED + CYCLE_FAIL))
fi

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
