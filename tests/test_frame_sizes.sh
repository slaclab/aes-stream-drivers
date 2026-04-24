#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Frame-Size Sweep Test.
#    Loops over a range of frame sizes (minimum PRBS-valid 12 bytes, 4K page
#    boundary, large, and cfgSize boundary) and verifies PRBS-clean loopback
#    at each.  Then sends a deliberately oversized frame and asserts the
#    driver rejects it.
#
# Environment variables:
#   DEV       Device path (default: /dev/datadev_0)
#   APP_BIN   Binary directory (default: data_dev/app/bin)
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
DURATION=3
FAILED=0
PASSED_SIZES=""

# Discover cfgSize up-front so both the drain and the sweep can reference it.
# Drain size must be >= every leftover frame still in flight from previous tests
# (kernel returns ret=-1 when the user buffer is smaller than the frame, and
# runRead breaks out on the resulting "Read Error" -- see data_dev/app/src/
# dmaLoopTest.cpp), so we use cfgSize as the drain size in every sub-test.
CFG_SIZE=$(cat /sys/module/datadev/parameters/cfgSize 2>/dev/null || echo 65536)

echo "=== Frame-size sweep test ==="
echo "DEV=$DEV cfgSize=$CFG_SIZE"

dump_log() { echo "--- begin $1 ---"; cat "$1"; echo "--- end $1 ---"; }

run_size_test() {
   local sz="$1"
   local out
   out=$(mktemp)

   # Drain stale frames from previous sub-tests at the MAX frame size so any
   # leftover large frame fits in the drain's read buffer (runRead allocates
   # size*2; a too-small buffer causes the kernel to drop the frame with
   # ret=-1 after one read, prematurely ending the drain).  -r 1 disables the
   # TX worker (pure-RX consumer) so the drain doesn't re-inject frames that
   # become stale when the timeout fires.  -d disables PRBS checking.
   timeout 3 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$CFG_SIZE" -r 1 -d > /dev/null 2>&1 || true
   sleep 1

   echo "  Testing size=$sz (${DURATION}s)..."
   timeout "$DURATION" "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$sz" > "$out" 2>&1
   local rc=$?

   if [ "$rc" -ne 124 ] && [ "$rc" -ne 0 ]; then
      echo "FAIL: dmaLoopTest exited $rc at size=$sz"
      dump_log "$out"
      rm -f "$out"
      return 1
   fi

   if grep -q "Prbs mismatch" "$out"; then
      echo "FAIL: PRBS mismatch at size=$sz"
      # Dump the full captured output (stderr included) so PrbsData diagnostics
      # ("Bad Sequence", "Bad value at index", "Bad size") are visible — they
      # distinguish LFSR drift from data corruption from frame-size mismatch.
      dump_log "$out"
      rm -f "$out"
      return 1
   fi

   if grep -q "Read Error" "$out"; then
      echo "FAIL: Read Error at size=$sz"
      grep "Read Error" "$out" | head -5
      rm -f "$out"
      return 1
   fi

   if grep -q "Write Error" "$out"; then
      echo "FAIL: Write Error at size=$sz"
      grep "Write Error" "$out" | head -5
      rm -f "$out"
      return 1
   fi

   # dmaLoopTest also exits 0 when open() of $DEV fails ("Error opening
   # device" from runWrite/runRead). $rc alone can't catch that path,
   # so guard it explicitly.
   if grep -q "Error opening device" "$out"; then
      echo "FAIL: Error opening device at size=$sz"
      grep "Error opening device" "$out" | head -5
      rm -f "$out"
      return 1
   fi

   rm -f "$out"
   return 0
}

# Valid sizes: 12 (minimum PRBS-valid), 4096, 32768, 65536 (cfgSize boundary).
# NOTE: 65536 is tested only if cfgSize >= 65536.
VALID_SIZES="12 4096 32768"

if [ "$CFG_SIZE" -ge 65536 ]; then
   VALID_SIZES="$VALID_SIZES 65536"
fi

for SZ in $VALID_SIZES; do
   if [ "$SZ" -gt "$CFG_SIZE" ]; then
      echo "  Skipping size=$SZ (exceeds cfgSize=$CFG_SIZE)"
      continue
   fi
   if run_size_test "$SZ"; then
      PASSED_SIZES="$PASSED_SIZES $SZ"
   else
      FAILED=$((FAILED + 1))
   fi
done

# Oversized rejection: cfgSize+1 should trigger "Write Error" from the driver.
OVERSIZE=$((CFG_SIZE + 1))
echo "  Testing oversized frame ($OVERSIZE > cfgSize=$CFG_SIZE)..."
OVER_OUT=$(mktemp)
timeout "$DURATION" "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$OVERSIZE" > "$OVER_OUT" 2>&1

if grep -q "Write Error" "$OVER_OUT"; then
   echo "  Oversized correctly rejected (Write Error)"
else
   echo "FAIL: oversized frame ($OVERSIZE) was NOT rejected"
   dump_log "$OVER_OUT"
   FAILED=$((FAILED + 1))
fi
rm -f "$OVER_OUT"

if [ "$FAILED" -eq 0 ]; then
   echo "PASS: frame_sizes -- valid sizes:$PASSED_SIZES; oversized rejected"
   exit 0
else
   echo "FAIL: frame_sizes -- $FAILED case(s) failed"
   exit 1
fi
