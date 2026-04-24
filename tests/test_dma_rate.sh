#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    DMA rate / throughput test. Runs dmaRate and verifies non-zero
#    throughput output before timeout.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Runs dmaRate against the datadev device and verifies it produces
# non-zero throughput output before timeout. In the emulator (TCG / QEMU)
# absolute rate numbers are meaningless -- this test only checks that the
# rate loop executed at least a few iterations.
#
# Timeout-as-success pattern: dmaRate runs forever on success.
#
# Environment variables:
#   DEV       Device path (default: /dev/datadev_0)
#   APP_BIN   Binary directory (default: data_dev/app/bin)
#   DURATION  Run duration in seconds (default: 10)
# ----------------------------------------------------------------------------

set -uo pipefail

DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"
DURATION="${DURATION:-10}"
# mktemp + trap so parallel invocations don't clobber each other and /tmp
# doesn't accumulate stale logs after repeated runs.
OUT=$(mktemp -t dma_rate_output.XXXXXX)
trap 'rm -f "$OUT"' EXIT

echo "=== DMA rate test ==="
echo "DEV=$DEV DURATION=${DURATION}s"

dump_log() { echo "--- begin $OUT ---"; cat "$OUT"; echo "--- end $OUT ---"; }

timeout "$DURATION" "$APP_BIN/dmaRate" -p "$DEV" -c 100 > "$OUT" 2>&1
RC=$?

# Timeout-as-success.
if [ "$RC" -ne 124 ] && [ "$RC" -ne 0 ]; then
   echo "FAIL: dmaRate exited $RC"
   dump_log
   exit 1
fi

# Primary check: output mentions rate/throughput/bytes with a non-zero value.
if grep -Ei "rate|MB/s|throughput|bytes" "$OUT" | grep -qE "[1-9]"; then
   echo "PASS: dmaRate produced non-zero rate/throughput output"
   exit 0
fi

# Fallback check: at minimum dmaRate should have produced multiple lines of
# output over the run duration -- a totally silent run means it did not work.
LINES=$(wc -l < "$OUT")
if [ "$LINES" -lt 10 ]; then
   echo "FAIL: dmaRate produced insufficient output ($LINES lines)"
   dump_log
   exit 1
fi

echo "PASS: DMA rate test (produced $LINES lines of output)"
exit 0
