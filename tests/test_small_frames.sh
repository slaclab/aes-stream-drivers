#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Small-Frame DMA Loopback Test (CPU cell).
#    Sweeps payload sizes from 1 to 4 bytes with random byte payloads to
#    exercise sub-word DMA transfers that PRBS validation cannot cover.
#    Uses dmaSmallFrameTest which does write-read-memcmp in a single process.
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
COUNT=100

echo "=== Small-frame DMA loopback (1-4 bytes, random payload) ==="
echo "DEV=$DEV COUNT=$COUNT"

OUT=$(mktemp)
trap "rm -f \$OUT" EXIT

"$APP_BIN/dmaSmallFrameTest" -p "$DEV" -c "$COUNT" -n 1 -x 4 > "$OUT" 2>&1
RC=$?

cat "$OUT"

if [ "$RC" -ne 0 ]; then
   echo "FAIL: dmaSmallFrameTest exited $RC"
   exit 1
fi

if grep -q "FAIL:" "$OUT"; then
   echo "FAIL: small_frames -- mismatch detected"
   exit 1
fi

echo "PASS: small_frames -- sizes 1-4 bytes verified with random payloads"
exit 0
