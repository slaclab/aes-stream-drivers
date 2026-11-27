#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    TX flags routing test. Sends frames whose AXIS flags word has the RX
#    timeout bit (flags[17]) set and verifies they still arrive on the
#    destination they were written to.
#
#    AxisG2_WriteTx used to OR flags[17] into bit[4] of the TX descriptor
#    word. The firmware (and the emulator) decode bits[7:4] of that word as
#    the DMA channel, so a frame written to dest 0 with flags[17] set was
#    routed to channel 1 (dest 256) instead. An application that forwards a
#    received frame with its flags unchanged hits this whenever the frame
#    was flagged as timed out by the driver.
#
#    The reader only opens dest 0. With the bug the frames land on dest 256,
#    which is not open, so the driver returns them to the free list and the
#    reader times out.
#
# Environment variables:
#   DEV           Device path (default: /dev/datadev_0)
#   APP_BIN       Binary directory (default: data_dev/app/bin)
#   SIZE          Frame size (default: 10000)
#   COUNT         Number of frames to send (default: 16)
#   READ_TIMEOUT  Seconds to wait for all frames (default: 20)
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
COUNT="${COUNT:-16}"
READ_TIMEOUT="${READ_TIMEOUT:-20}"

# flags[17] is the RX timeout bit the driver sets on received frames
# (AxisG2_Process). It has no TX meaning and must be ignored on write.
FLAGS=0x20000

RD_OUT=$(mktemp)
WR_OUT=$(mktemp)
trap 'rm -f "$RD_OUT" "$WR_OUT"' EXIT

echo "=== TX flags routing test (flags=$FLAGS, dest 0) ==="
echo "DEV=$DEV SIZE=$SIZE COUNT=$COUNT"

dump_log() {
   echo "--- begin dmaRead output ---";  cat "$RD_OUT"; echo "--- end dmaRead output ---"
   echo "--- begin dmaWrite output ---"; cat "$WR_OUT"; echo "--- end dmaWrite output ---"
}

# Drain stale frames left by earlier tests so dmaRead's -c count only sees
# frames generated below. Pure-RX consumer (-r 1 disables TX worker, -d
# disables PRBS check); matches the drain pattern in test_idx_loopback.sh.
timeout 3 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" -r 1 -d > /dev/null 2>&1 || true
sleep 1

# Reader first so no frame is dropped before the dest 0 mask is set. -w makes
# it select() instead of spinning on the non-blocking read; -b prints one
# header line per frame carrying the dest and the PRBS verdict.
timeout "$READ_TIMEOUT" "$APP_BIN/dmaRead" -p "$DEV" -m 0 -w -e -b -c "$COUNT" > "$RD_OUT" 2>&1 &
RD_PID=$!
sleep 1

timeout "$READ_TIMEOUT" "$APP_BIN/dmaWrite" -p "$DEV" -e -s "$SIZE" -c "$COUNT" -f "$FLAGS" 0 > "$WR_OUT" 2>&1
WR_RC=$?

wait "$RD_PID"
RD_RC=$?

if [ "$WR_RC" -ne 0 ]; then
   echo "FAIL: dmaWrite exited $WR_RC"
   dump_log
   exit 1
fi

if [ "$RD_RC" -eq 124 ]; then
   echo "FAIL: dmaRead did not receive $COUNT frames on dest 0 within ${READ_TIMEOUT}s"
   echo "      (flags[17] leaked into the TX descriptor channel field?)"
   dump_log
   exit 1
elif [ "$RD_RC" -ne 0 ]; then
   echo "FAIL: dmaRead exited $RD_RC"
   dump_log
   exit 1
fi

if grep -qE "Read failed|prbs=0" "$RD_OUT"; then
   echo "FAIL: dmaRead reported a read error or PRBS mismatch"
   grep -E "Read failed|prbs=0" "$RD_OUT" | head -5
   dump_log
   exit 1
fi

# Every header line must report dest 0; dmaRead -b prints "Dest=<n>," per frame.
RX_DEST0=$(grep -c "Dest=0," "$RD_OUT")
if [ "$RX_DEST0" -ne "$COUNT" ]; then
   echo "FAIL: expected $COUNT frames on dest 0, got $RX_DEST0"
   dump_log
   exit 1
fi

echo "PASS: $COUNT frames with flags=$FLAGS received on dest 0"
exit 0
