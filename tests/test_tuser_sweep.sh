#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    AXI tuser Sideband Sweep Test.
#    Exercises extreme fuser/luser values to catch bit-shifting or masking
#    bugs in axisSetFlags / axisGetFuser / axisGetLuser (AxisDriver.h).
#
#    dmaLoopTest already validates that received fuser/luser match the
#    transmitted values; a mismatch prints "Read Error".
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
DURATION=3
FAILED=0

echo "=== AXI tuser sideband sweep ==="
echo "DEV=$DEV SIZE=$SIZE"

run_tuser_case() {
   local fuser="$1"
   local luser="$2"
   local out
   out=$(mktemp)

   # Drain stale frames from previous tests: pure-RX consumer (-r 1
   # disables TX worker, -d disables PRBS check) so the drain doesn't
   # re-inject frames that become stale when the timeout fires.
   timeout 3 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" -f "$fuser" -l "$luser" -r 1 -d > /dev/null 2>&1 || true
   sleep 1

   echo "  Testing fuser=$fuser luser=$luser (${DURATION}s)..."
   timeout "$DURATION" "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" -f "$fuser" -l "$luser" > "$out" 2>&1
   local rc=$?

   if [ "$rc" -ne 124 ] && [ "$rc" -ne 0 ]; then
      echo "FAIL: dmaLoopTest exited $rc (fuser=$fuser luser=$luser)"
      cat "$out"
      rm -f "$out"
      return 1
   fi

   # Combined error-string grep: dmaLoopTest exits 0 even when a worker
   # thread hits Read Error, Write Error, or Error opening device (the
   # thread just flips running=false and main exits cleanly — see
   # runWrite/runRead in data_dev/app/src/dmaLoopTest.cpp), so $rc alone
   # can't distinguish pass from fail.
   if grep -qE "Read Error|Write Error|Error opening device" "$out"; then
      echo "FAIL: dmaLoopTest worker reported an error (fuser=$fuser luser=$luser)"
      grep -E "Read Error|Write Error|Error opening device" "$out" | head -5
      rm -f "$out"
      return 1
   fi

   if grep -q "Prbs mismatch" "$out"; then
      echo "FAIL: PRBS mismatch with fuser=$fuser luser=$luser"
      grep "Prbs mismatch" "$out" | head -5
      rm -f "$out"
      return 1
   fi

   rm -f "$out"
   return 0
}

run_tuser_case 0x0 0xFF  || FAILED=$((FAILED + 1))
run_tuser_case 0xFF 0x0  || FAILED=$((FAILED + 1))

if [ "$FAILED" -eq 0 ]; then
   echo "PASS: tuser sweep -- both extreme fuser/luser combos clean"
   exit 0
else
   echo "FAIL: tuser sweep -- $FAILED case(s) failed"
   exit 1
fi
