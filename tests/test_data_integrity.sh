#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Data integrity test. Runs dmaLoopTest and verifies >= 100 consecutive
#    DMA loopback transfers complete with zero PRBS mismatches.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Runs dmaLoopTest and verifies >= 100 consecutive DMA loopback transfers
# complete with zero PRBS mismatches. This provides byte-level data integrity
# validation end-to-end through the emulator DMA engine.
#
# Environment variables:
#   DEV           Device path (default: /dev/datadev_0)
#   APP_BIN       Binary directory (default: data_dev/app/bin)
#   MIN_TRANSFERS Minimum number of transfers required (default: 100)
# ----------------------------------------------------------------------------

set -uo pipefail

DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"
MIN_TRANSFERS=100
SIZE="${SIZE:-10000}"
TMPFILE=$(mktemp)
trap "rm -f $TMPFILE" EXIT

echo "=== Data integrity check (min ${MIN_TRANSFERS} transfers) ==="
echo "DEV=$DEV APP_BIN=$APP_BIN SIZE=$SIZE"

dump_log() { echo "--- begin dmaLoopTest output ---"; cat "$TMPFILE"; echo "--- end dmaLoopTest output ---"; }

timeout 60 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" > "$TMPFILE" 2>&1

if grep -q "Prbs mismatch" "$TMPFILE"; then
   echo "FAIL: PRBS data mismatch detected"
   grep "Prbs mismatch" "$TMPFILE" | head -5
   dump_log
   exit 1
fi

TX_COUNT=$(grep "TxCount:" "$TMPFILE" | tail -1 | grep -oP 'TxCount:\s*\K[0-9]+')
if [ -z "$TX_COUNT" ]; then
   echo "FAIL: No TxCount found in output"
   dump_log
   exit 1
fi

if [ "$TX_COUNT" -ge "$MIN_TRANSFERS" ]; then
   echo "PASS: $TX_COUNT transfers with zero PRBS mismatches (>= $MIN_TRANSFERS required)"
   exit 0
else
   echo "FAIL: Only $TX_COUNT transfers completed (need >= $MIN_TRANSFERS)"
   dump_log
   exit 1
fi
