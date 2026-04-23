#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Index-Based / Zero-Copy RX Loopback Test.
#    Runs dmaLoopTest with -i (index enable) to exercise the mmap zero-copy
#    receive path: dmaMapDma + dmaReadIndex + dmaRetIndex.  Without -i, only
#    the malloc + copy_to_user path is covered.
#
#    Timeout-as-success pattern: exit 124 (killed by timeout) means "ran
#    without error for the full duration."
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
OUT=$(mktemp)
trap "rm -f \$OUT" EXIT

echo "=== Index-based (zero-copy) loopback test ==="
echo "DEV=$DEV SIZE=$SIZE DURATION=${DURATION}s"

timeout "$DURATION" "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" -i > "$OUT" 2>&1
RC=$?

dump_log() { echo "--- begin idx_loopback output ---"; cat "$OUT"; echo "--- end idx_loopback output ---"; }

if [ "$RC" -ne 124 ] && [ "$RC" -ne 0 ]; then
   echo "FAIL: dmaLoopTest -i exited $RC"
   dump_log
   exit 1
fi

if grep -q "Prbs mismatch" "$OUT"; then
   echo "FAIL: PRBS mismatch in zero-copy path"
   grep "Prbs mismatch" "$OUT" | head -5
   dump_log
   exit 1
fi

if grep -q "Read Error" "$OUT"; then
   echo "FAIL: Read Error in zero-copy path"
   grep "Read Error" "$OUT" | head -5
   dump_log
   exit 1
fi

echo "PASS: index-based zero-copy loopback (${DURATION}s, no errors)"
exit 0
