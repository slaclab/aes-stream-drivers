#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Backpressure / buffer starvation test. Reloads datadev with low buffer
#    counts and runs dmaLoopTest to exercise backpressure code paths.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Reloads datadev with low buffer counts (cfgTxCount=4 cfgRxCount=4) and
# runs dmaLoopTest at 8x frame-per-buffer ratio to exercise backpressure
# code paths in the DMA engine.
#
# Requires root/sudo -- this test reloads kernel modules.
#
# Environment variables:
#   DEV          Device path (default: /dev/datadev_0)
#   APP_BIN      Binary directory (default: data_dev/app/bin)
#   DATADEV_KO   Path to datadev.ko (default: data_dev/driver/datadev.ko)
#   TIMEOUT_SEC  Module init timeout (default: 15)
# ----------------------------------------------------------------------------

set -uo pipefail

DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"
DATADEV_KO="${DATADEV_KO:-data_dev/driver/datadev.ko}"
TIMEOUT_SEC="${TIMEOUT_SEC:-15}"
INSMOD_TIMEOUT_SEC="${INSMOD_TIMEOUT_SEC:-120}"

# Require root/sudo capability. Skip gracefully if neither is available so
# this script can be dry-run on unprivileged hosts without breaking CI.
if [ "$(id -u)" -ne 0 ] && ! sudo -n true 2>/dev/null; then
   echo "SKIP: test_backpressure.sh requires root/sudo (no TTY sudo allowed)"
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

echo "=== Backpressure / buffer starvation test ==="

# Snapshot dmesg line count before test to isolate the delta later.
DMESG_BEFORE=$($SUDO dmesg | wc -l)

# Unconditional rmmod to establish known state before reload.
$SUDO rmmod datadev 2>/dev/null || true
for _ in $(seq 1 15); do [ ! -e "$DEV" ] && break; sleep 0.5; done

# Reload with low buffer counts to trigger backpressure paths.
timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" cfgTxCount=4 cfgRxCount=4 cfgSize=65536 cfgDebug=1

# Wait for driver to reach live state.
timeout $TIMEOUT_SEC bash -c "until [ \"\$(cat /sys/module/datadev/initstate 2>/dev/null)\" = live ]; do sleep 0.5; done" || {
   echo "[FAIL] backpressure -- datadev did not reach live state"
   exit 1
}

# Ensure /dev node exists (Docker containers may lack udev).
if [ ! -e "$DEV" ]; then
   DATADEV_MAJOR=$(awk '$2 == "datadev_0" { print $1 }' /proc/devices)
   $SUDO mknod "$DEV" c "$DATADEV_MAJOR" 0
   $SUDO chmod 666 "$DEV"
fi
$SUDO chmod 666 "$DEV"

# Give the DMA engine time to complete the emulator seed phase.
sleep 2

# Run dmaLoopTest with 32768-byte frames; let it run until timeout (rc=124 ok).
TMPFILE=$(mktemp)
trap "rm -f \$TMPFILE" EXIT
timeout 30 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" > "$TMPFILE" 2>&1
LOOP_RC=$?

# PRBS integrity check.
if grep -q "Prbs mismatch" "$TMPFILE"; then
   echo "[FAIL] backpressure -- PRBS mismatch"
   grep "Prbs mismatch" "$TMPFILE" | head -5
   FAILED=$((FAILED + 1))
fi

# Kernel error check against dmesg delta.
DMESG_DELTA=$($SUDO dmesg | tail -n "+$((DMESG_BEFORE + 1))")
if echo "$DMESG_DELTA" | grep -iE 'oops|panic|BUG:|WARNING:'; then
   echo "[FAIL] backpressure -- kernel error in dmesg"
   FAILED=$((FAILED + 1))
fi

if [ "$FAILED" -eq 0 ]; then
   echo "[PASS] backpressure"
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
