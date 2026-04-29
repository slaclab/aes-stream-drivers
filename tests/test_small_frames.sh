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
#   SIZE      Drain frame size (default: 32768; run_tests.sh exports one)
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
SIZE="${SIZE:-32768}"
COUNT=100

echo "=== Small-frame DMA loopback (1-4 bytes, random payload) ==="
echo "DEV=$DEV COUNT=$COUNT"

# Drain stale frames from previous tests (e.g. test_frame_sizes leaves
# 12/4096/32768/65536-byte frames on dest 0). dmaSmallFrameTest does
# write-read-memcmp sequentially, so any leftover RX frame with size
# outside 1..4 would make its first dmaRead return an unexpected length
# and fail the run. Pure-RX consumer
# (-r 1 disables TX worker, -d disables PRBS check) so the drain doesn't
# re-inject frames that become stale when the timeout fires. Matches the
# drain pattern in test_frame_sizes.sh / test_tuser_sweep.sh /
# test_concurrent_open.sh.
timeout 3 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" -r 1 -d > /dev/null 2>&1 || true
sleep 1

OUT=$(mktemp)
# Single quotes defer $OUT expansion to trap-fire time; inner double
# quotes protect against whitespace/glob chars if $TMPDIR is ever unusual.
trap 'rm -f "$OUT"' EXIT

# Outer `timeout` guards against a CI hang if dmaSmallFrameTest wedges
# despite its internal 5 s per-op select() timeouts (e.g. a blocking
# ioctl/read/write in a wedged driver path). Matches the timeout-wrapping
# convention used throughout the rest of the suite (test_data_integrity.sh,
# test_irq_modes.sh, test_backpressure.sh, ...). 60 s is ample headroom:
# even the worst case (4 sizes × 100 frames × two 5 s selects) can only
# reach ~4000 s if every op blocks maximally, which is a fault we want to
# fail fast on. rc=124 is the timeout exit code; the existing non-zero
# check below surfaces it as a test failure.
timeout 60 "$APP_BIN/dmaSmallFrameTest" -p "$DEV" -c "$COUNT" -n 1 -x 4 > "$OUT" 2>&1
RC=$?

cat "$OUT"

if [ "$RC" -eq 124 ]; then
   echo "FAIL: dmaSmallFrameTest timed out (exit 124)"
   exit 1
fi
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
