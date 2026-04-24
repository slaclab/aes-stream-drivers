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
# Honor the MIN_TRANSFERS env-var contract documented in the header comment
# above so standalone runs can tighten or relax the pass threshold without
# editing the script (run_tests.sh does not set this).
MIN_TRANSFERS="${MIN_TRANSFERS:-100}"
SIZE="${SIZE:-10000}"
TMPFILE=$(mktemp)
# Single quotes defer $TMPFILE expansion to trap-fire time; inner double
# quotes protect against whitespace/glob chars if TMPDIR is ever unusual.
trap 'rm -f "$TMPFILE"' EXIT

echo "=== Data integrity check (min ${MIN_TRANSFERS} transfers) ==="
echo "DEV=$DEV APP_BIN=$APP_BIN SIZE=$SIZE"

dump_log() { echo "--- begin dmaLoopTest output ---"; cat "$TMPFILE"; echo "--- end dmaLoopTest output ---"; }

timeout 60 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" > "$TMPFILE" 2>&1

# dmaLoopTest returns 0 even when a worker thread hit an unrecoverable
# error: runWrite/runRead just flip running=false and the main loop exits
# cleanly (see data_dev/app/src/dmaLoopTest.cpp). These three grep guards
# catch the cases where TxCount alone can't distinguish pass from fail.
if grep -qE "Read Error|Write Error|Error opening device" "$TMPFILE"; then
   echo "FAIL: dmaLoopTest worker thread reported an error"
   grep -E "Read Error|Write Error|Error opening device" "$TMPFILE" | head -5
   dump_log
   exit 1
fi

if grep -q "Prbs mismatch" "$TMPFILE"; then
   echo "FAIL: PRBS data mismatch detected"
   grep "Prbs mismatch" "$TMPFILE" | head -5
   dump_log
   exit 1
fi

# awk extraction avoids grep -oP (PCRE): GNU grep has it, but musl/BusyBox
# builds don't, and -P failed silently on some distro minimal images.
TX_COUNT=$(grep "TxCount:" "$TMPFILE" | tail -1 | awk -F: '{gsub(/[^0-9]/,"",$2); print $2}')
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
