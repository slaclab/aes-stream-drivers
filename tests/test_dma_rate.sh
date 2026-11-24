#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    DMA rate / throughput test. Runs dmaRate against emulator loopback
#    traffic and verifies it reports at least one completed batch.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# dmaRate is a pure RX consumer, so dmaWrite feeds frames through the
# emulator loopback while dmaRate runs in the background. dmaRate prints one
# result line each time it has consumed -c frames, so the writer sends at
# least twice that many to guarantee a result line appears.
#
# Regression coverage: dmaRate requests MAX_RET_CNT_C (1000) entries per
# dmaReadBulkIndex call. The driver rejects a bulk read larger than
# cfgRxCount + cfgTxCount with -EINVAL and logs "attempted to read too many
# buffers", so on the CI buffer counts (64 + 64) the read loop never
# advanced and never printed a result line. dmaRate now clamps the request
# to the mapped buffer count; this test fails if either symptom returns.
#
# Timeout-as-success pattern: dmaRate runs forever on success, so exit 124
# from timeout is expected. In the emulator (TCG / QEMU) absolute rate
# numbers are meaningless; only the presence of a result line is checked.
#
# Environment variables:
#   DEV       Device path (default: /dev/datadev_0)
#   APP_BIN   Binary directory (default: data_dev/app/bin)
#   SIZE      Frame size (default: 10000)
#   DURATION  dmaRate run duration in seconds (default: 10)
# -----------------------------------------------------------------------------

set -uo pipefail

DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"
SIZE="${SIZE:-10000}"
DURATION="${DURATION:-10}"
# Frames per dmaRate result line, and total frames the writer sends.
RATE_COUNT=200
WR_COUNT=$((RATE_COUNT * 4))
WR_TIMEOUT=$((DURATION + 10))

if [ "$(id -u)" -eq 0 ]; then
   SUDO=""
else
   SUDO="sudo -n"
fi

# mktemp + trap so parallel invocations don't clobber each other and /tmp
# doesn't accumulate stale logs after repeated runs.
RT_OUT=$(mktemp -t dma_rate_output.XXXXXX)
WR_OUT=$(mktemp -t dma_rate_write.XXXXXX)
trap 'rm -f "$RT_OUT" "$WR_OUT"' EXIT

echo "=== DMA rate test ==="
echo "DEV=$DEV SIZE=$SIZE DURATION=${DURATION}s RATE_COUNT=$RATE_COUNT WR_COUNT=$WR_COUNT"

dump_log() {
   echo "--- begin dmaRate output ---";  cat "$RT_OUT"; echo "--- end dmaRate output ---"
   echo "--- begin dmaWrite output ---"; cat "$WR_OUT"; echo "--- end dmaWrite output ---"
}

# Drain stale frames left by earlier tests so the frames counted below are
# only the ones generated here. Pure-RX consumer (-r 1 disables TX worker,
# -d disables PRBS check); matches the drain pattern in test_idx_loopback.sh.
timeout 3 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" -r 1 -d > /dev/null 2>&1 || true
sleep 1

# dmesg may be unreadable on hardened hosts (kernel.dmesg_restrict without
# sudo). Record the baseline only when readable; otherwise skip that assertion.
DMESG_OK=1
if ! $SUDO dmesg > /dev/null 2>&1; then
   DMESG_OK=0
   echo "WARN: dmesg not readable; skipping 'too many buffers' check"
fi
DMESG_BEFORE=$($SUDO dmesg 2>/dev/null | wc -l)

# Reader first so no frame is dropped before the mask is set.
timeout "$DURATION" "$APP_BIN/dmaRate" -p "$DEV" -c "$RATE_COUNT" > "$RT_OUT" 2>&1 &
RT_PID=$!
sleep 1

timeout "$WR_TIMEOUT" "$APP_BIN/dmaWrite" -p "$DEV" -e -s "$SIZE" -c "$WR_COUNT" 0 > "$WR_OUT" 2>&1
WR_RC=$?

wait "$RT_PID"
RT_RC=$?

if [ "$WR_RC" -ne 0 ]; then
   echo "FAIL: dmaWrite exited $WR_RC"
   dump_log
   exit 1
fi

# Timeout-as-success for dmaRate.
if [ "$RT_RC" -ne 124 ] && [ "$RT_RC" -ne 0 ]; then
   echo "FAIL: dmaRate exited $RT_RC"
   dump_log
   exit 1
fi

if [ "$DMESG_OK" -eq 1 ]; then
   DMESG_DELTA=$($SUDO dmesg | tail -n "+$((DMESG_BEFORE + 1))")
   if printf '%s\n' "$DMESG_DELTA" | grep -q "attempted to read too many buffers"; then
      echo "FAIL: driver rejected dmaRate bulk read (request exceeds buffer count)"
      printf '%s\n' "$DMESG_DELTA" | grep "attempted to read too many buffers" | head -5
      dump_log
      exit 1
   fi
fi

# A result line starts with the maxCnt column (an integer) followed by the
# last frame size in %e format; the header line has no digits.
RESULT_LINES=$(grep -cE '^\s*[0-9]+\s+[0-9]\.[0-9]+e[+-][0-9]+' "$RT_OUT")
if [ "$RESULT_LINES" -lt 1 ]; then
   echo "FAIL: dmaRate never completed a batch of $RATE_COUNT frames (read loop stalled)"
   dump_log
   exit 1
fi

echo "PASS: dmaRate reported $RESULT_LINES completed batch(es) of $RATE_COUNT frames"
exit 0
