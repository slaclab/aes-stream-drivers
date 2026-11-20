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
#    Phase 2 covers the bulk index path used by dmaRead -i (dmaReadBulkIndex
#    + dmaRetIndexes): dmaWrite feeds more frames than cfgRxCount through the
#    emulator loopback while dmaRead -i -c consumes them.  dmaRead must exit
#    on its own (an RX buffer leak stalls it and trips the timeout) and the
#    driver must not log "Invalid index posted", which is what happens when
#    dmaRetIndexes is handed indexes the read did not fill.
#
# Environment variables:
#   DEV       Device path (default: /dev/datadev_0)
#   APP_BIN   Binary directory (default: data_dev/app/bin)
#   SIZE      Frame size (default: 10000)
#   RX_COUNT  Driver cfgRxCount (default: /tmp/ci_cfg_rx_count, else 64)
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
# Single quotes defer $OUT expansion to trap-fire time; inner double
# quotes protect against whitespace/glob chars if $TMPDIR is ever unusual.
trap 'rm -f "$OUT"' EXIT

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

# dmaLoopTest returns 0 even when a worker hit Read Error / Write Error /
# Error opening device (the thread just sets running=false and main exits
# cleanly). Explicit grep guards below catch those silent-exit paths.
if grep -qE "Read Error|Write Error|Error opening device" "$OUT"; then
   echo "FAIL: dmaLoopTest worker thread reported an error"
   grep -E "Read Error|Write Error|Error opening device" "$OUT" | head -5
   dump_log
   exit 1
fi

echo "PASS: index-based zero-copy loopback (${DURATION}s, no errors)"

# ---------------------------------------------------------------------------
# Phase 2: dmaRead -i bulk index return
# ---------------------------------------------------------------------------
# Frame count must exceed cfgRxCount so a leaked RX buffer per batch is
# guaranteed to exhaust the ring before dmaRead reaches its -c target.
if [ -z "${RX_COUNT:-}" ]; then
   if [ -r /tmp/ci_cfg_rx_count ]; then
      RX_COUNT=$(cat /tmp/ci_cfg_rx_count)
   else
      RX_COUNT=64
   fi
fi
COUNT=$((RX_COUNT * 4))
READ_TIMEOUT=30

if [ "$(id -u)" -eq 0 ]; then
   SUDO=""
else
   SUDO="sudo -n"
fi

echo "=== dmaRead -i bulk index return test ==="
echo "DEV=$DEV SIZE=$SIZE COUNT=$COUNT (cfgRxCount=$RX_COUNT)"

# Drain stale frames left by phase 1 so dmaRead's -c count and PRBS check
# only see frames generated below. Pure-RX consumer (-r 1 disables TX
# worker, -d disables PRBS check); matches the drain pattern in
# test_small_frames.sh / test_frame_sizes.sh.
timeout 3 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" -r 1 -d > /dev/null 2>&1 || true
sleep 1

# dmesg may be unreadable on hardened hosts (kernel.dmesg_restrict without
# sudo). Record the baseline only when readable; otherwise skip that assertion.
DMESG_OK=1
if ! $SUDO dmesg > /dev/null 2>&1; then
   DMESG_OK=0
   echo "WARN: dmesg not readable; skipping 'Invalid index posted' check"
fi
DMESG_BEFORE=$($SUDO dmesg 2>/dev/null | wc -l)

RD_OUT=$(mktemp)
WR_OUT=$(mktemp)
trap 'rm -f "$OUT" "$RD_OUT" "$WR_OUT"' EXIT

# Reader first so no frame is dropped before the mask is set. -w makes it
# select() instead of spinning on the non-blocking read; -b prints one
# header line per frame carrying the PRBS verdict (prbs=0 is a mismatch).
timeout "$READ_TIMEOUT" "$APP_BIN/dmaRead" -p "$DEV" -m 0 -i -w -e -b -c "$COUNT" > "$RD_OUT" 2>&1 &
RD_PID=$!
sleep 1

timeout "$READ_TIMEOUT" "$APP_BIN/dmaWrite" -p "$DEV" -e -s "$SIZE" -c "$COUNT" 0 > "$WR_OUT" 2>&1
WR_RC=$?

wait "$RD_PID"
RD_RC=$?

dump_phase2() {
   echo "--- begin dmaRead output ---";  cat "$RD_OUT"; echo "--- end dmaRead output ---"
   echo "--- begin dmaWrite output ---"; cat "$WR_OUT"; echo "--- end dmaWrite output ---"
}

if [ "$WR_RC" -ne 0 ]; then
   echo "FAIL: dmaWrite exited $WR_RC"
   dump_phase2
   exit 1
fi

if [ "$RD_RC" -eq 124 ]; then
   echo "FAIL: dmaRead -i did not receive $COUNT frames within ${READ_TIMEOUT}s (RX stall)"
   dump_phase2
   exit 1
elif [ "$RD_RC" -ne 0 ]; then
   echo "FAIL: dmaRead -i exited $RD_RC"
   dump_phase2
   exit 1
fi

if grep -qE "Read failed|prbs=0" "$RD_OUT"; then
   echo "FAIL: dmaRead -i reported a read error or PRBS mismatch"
   grep -E "Read failed|prbs=0" "$RD_OUT" | head -5
   dump_phase2
   exit 1
fi

if [ "$DMESG_OK" -eq 1 ]; then
   DMESG_DELTA=$($SUDO dmesg | tail -n "+$((DMESG_BEFORE + 1))")
   if printf '%s\n' "$DMESG_DELTA" | grep -q "Invalid index posted"; then
      echo "FAIL: driver logged 'Invalid index posted' during dmaRead -i"
      printf '%s\n' "$DMESG_DELTA" | grep "Invalid index posted" | head -5
      dump_phase2
      exit 1
   fi
fi

echo "PASS: dmaRead -i returned $COUNT frames with no invalid index"
exit 0
