#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Concurrent-Process Access Test.
#    Backgrounds two dmaLoopTest instances on different destinations (0 and 1)
#    to stress per-fd vs shared-device state under concurrent access.
#    Existing tests only open the device from a single process.
#
#    Note: dmaIoctlTest is intentionally excluded — it claims dest 0 via
#    dmaSetMaskBytes which conflicts with the concurrent dmaLoopTest on
#    dest 0 (the kernel enforces exclusive dest ownership per fd).
#
#    Timeout-as-success: dmaLoopTest exit 124 means "no errors for full
#    duration."
#
# Environment variables:
#   DEV       Device path (default: /dev/datadev_0)
#   APP_BIN   Binary directory (default: data_dev/app/bin)
#   SIZE      Frame size (default: 10000)
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------

set -uo pipefail

DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"
SIZE="${SIZE:-10000}"
DURATION=5
FAILED=0

echo "=== Concurrent-process access test ==="
echo "DEV=$DEV SIZE=$SIZE DURATION=${DURATION}s"

# Drain stale frames from previous tests: pure-RX consumer (-r 1 disables
# TX worker, -d disables PRBS check) so the drain doesn't re-inject frames
# that become stale when the timeout fires.
timeout 3 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" -r 1 -d > /dev/null 2>&1 || true
sleep 1

OUT_D0=$(mktemp)
OUT_D1=$(mktemp)
# Single quotes defer expansion until trap-fire; inner double quotes protect
# against whitespace/glob chars if $TMPDIR is ever unusual.
trap 'rm -f "$OUT_D0" "$OUT_D1"' EXIT

dump_log() { echo "--- begin $1 ---"; cat "$1"; echo "--- end $1 ---"; }

# Launch two dmaLoopTest processes on different destinations.
timeout "$DURATION" "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" > "$OUT_D0" 2>&1 &
PID_D0=$!

timeout "$DURATION" "$APP_BIN/dmaLoopTest" -p "$DEV" -m 1 -s "$SIZE" > "$OUT_D1" 2>&1 &
PID_D1=$!

# Wait for both processes.
wait "$PID_D0"
RC_D0=$?
wait "$PID_D1"
RC_D1=$?

# Validate dmaLoopTest dest 0.
if [ "$RC_D0" -ne 124 ] && [ "$RC_D0" -ne 0 ]; then
   echo "FAIL: dmaLoopTest dest=0 exited $RC_D0"
   dump_log "$OUT_D0"
   FAILED=$((FAILED + 1))
elif grep -qE "Prbs mismatch|Read Error|Write Error|Error opening device" "$OUT_D0"; then
   echo "FAIL: dmaLoopTest dest=0 had errors"
   grep -E "Prbs mismatch|Read Error|Write Error|Error opening device" "$OUT_D0" | head -5
   FAILED=$((FAILED + 1))
fi

# Validate dmaLoopTest dest 1.
if [ "$RC_D1" -ne 124 ] && [ "$RC_D1" -ne 0 ]; then
   echo "FAIL: dmaLoopTest dest=1 exited $RC_D1"
   dump_log "$OUT_D1"
   FAILED=$((FAILED + 1))
elif grep -qE "Prbs mismatch|Read Error|Write Error|Error opening device" "$OUT_D1"; then
   echo "FAIL: dmaLoopTest dest=1 had errors"
   grep -E "Prbs mismatch|Read Error|Write Error|Error opening device" "$OUT_D1" | head -5
   FAILED=$((FAILED + 1))
fi

if [ "$FAILED" -eq 0 ]; then
   echo "PASS: concurrent_open -- 2 concurrent dmaLoopTest processes ran without errors"
   exit 0
else
   echo "FAIL: concurrent_open -- $FAILED process(es) had errors"
   exit 1
fi
