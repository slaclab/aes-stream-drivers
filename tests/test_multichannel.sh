#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Multi-channel DMA routing test. Runs dmaLoopTest against three
#    destination channels and verifies traffic flow without cross-contamination.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Runs dmaLoopTest against three destination channels (0, 7, 8) and verifies:
# 1. All destinations report non-zero RxCount (traffic flowed to each)
# 2. No "Read Error" messages (no cross-contamination between channels)
#
# Dests 7 and 8 cross the byte boundary in the 512-byte destination mask,
# exercising the byte-boundary code in dmaAddMaskBytes.  Three dests is
# the practical maximum for the emulator's 64-buffer pool.
#
# Timeout-as-success pattern: dmaLoopTest runs forever on success; the
# harness uses `timeout` to bound runtime, and exit code 124 means "no
# errors found". Any other non-zero exit indicates a thread detected a
# mismatch and is treated as failure.
#
# Environment variables:
#   DEV       Device path (default: /dev/datadev_0)
#   APP_BIN   Binary directory (default: data_dev/app/bin)
#   DURATION  Run duration in seconds (default: 15)
#   SIZE      Frame size (default: 10000)
# ----------------------------------------------------------------------------

set -uo pipefail

DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"
DURATION="${DURATION:-15}"
SIZE="${SIZE:-10000}"
OUT="/tmp/multichannel_output.txt"

DESTS="0,7,8"
NUM_DESTS=3

echo "=== Multi-channel DMA routing test (dests $DESTS) ==="
echo "DEV=$DEV DURATION=${DURATION}s SIZE=$SIZE"

# Run dmaLoopTest with multiple destinations. Capture full output to file; dump only on failure.
dump_log() { echo "--- begin $OUT ---"; cat "$OUT"; echo "--- end $OUT ---"; }

timeout "$DURATION" "$APP_BIN/dmaLoopTest" -p "$DEV" -m "$DESTS" -s "$SIZE" > "$OUT" 2>&1
RC=$?

# Timeout-as-success: 124 (timeout killed it) or 0 (clean exit) are good.
if [ "$RC" -ne 124 ] && [ "$RC" -ne 0 ]; then
   echo "FAIL: dmaLoopTest exited $RC"
   dump_log
   exit 1
fi

# Cross-contamination guard: dmaLoopTest prints "Read Error" when a reader
# receives data from a different destination than it expected.
if grep -q "Read Error" "$OUT"; then
   echo "FAIL: Read Error detected (cross-contamination between dests)"
   grep "Read Error" "$OUT" | head -5
   dump_log
   exit 1
fi

# Also guard against PRBS mismatches or errors opening the device.
if grep -q "Prbs mismatch" "$OUT"; then
   echo "FAIL: PRBS mismatch detected"
   grep "Prbs mismatch" "$OUT" | head -5
   dump_log
   exit 1
fi

# Extract the final "RxCount:" line (dmaLoopTest prints one per second).
# Format: "RxCount:      <count0>      <count1> ..."
LAST_RX=$(grep "^RxCount:" "$OUT" | tail -1 || true)
if [ -z "$LAST_RX" ]; then
   echo "FAIL: no RxCount line found in output (test may not have run long enough)"
   dump_log
   exit 1
fi
echo "Final stats: $LAST_RX"

# Extract the per-destination RxCount values (skip field 1 which is label).
COUNTS=$(echo "$LAST_RX" | awk '{for (i=2; i<=NF; i++) print $i}')

# Count how many destinations received data (RxCount > 0).
NZ=$(echo "$COUNTS" | awk '$1 > 0' | wc -l)
if [ "$NZ" -lt "$NUM_DESTS" ]; then
   echo "FAIL: fewer than $NUM_DESTS destinations received data (non-zero RxCount count=$NZ)"
   echo "Per-dest counts:"
   echo "$COUNTS"
   dump_log
   exit 1
fi

echo "PASS: multi-channel test -- all $NUM_DESTS destinations received data with no cross-contamination"
exit 0
