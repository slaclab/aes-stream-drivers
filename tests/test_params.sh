#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Module parameter validation test. Reloads datadev with custom buffer
#    counts and sizes, then verifies the parameters via /proc/datadev_0.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Reloads the datadev module with custom cfgTxCount / cfgRxCount / cfgSize
# values and verifies the new parameters are honored via /proc/datadev_0
# output (Buffer Count and Buffer Size in both RX and TX sections).
#
# Also performs a load/unload cycle with cfgTxCount=0 and another with
# cfgRxCount=0. A zero count leaves the DmaBufferList indexed array NULL,
# and dmaFreeBuffers() must tolerate that on rmmod.
#
# Also attempts a load with an unsupported cfgMode, which must fail the
# insmod and leave no module loaded.
#
# Finally reloads with cfgSize=262144 and loops a 200000-byte frame. The
# emulator must bound TX frames by the maxSize register the driver wrote
# (cfgSize), not its 128 KiB reset constant, or the frame is dropped with
# no RX completion.
#
# Requires root/sudo -- this test reloads kernel modules. NEVER run on the
# host during local development; only inside CI (which has sudo) or inside
# the QEMU VM (which has full root).
#
# Environment variables:
#   DEV          Device path (default: /dev/datadev_0)
#   APP_BIN      Binary directory (default: data_dev/app/bin)
#   DATADEV_KO   Path to datadev.ko module
#                (default: data_dev/driver/datadev.ko)
#   CUSTOM_TX    cfgTxCount override (default: 256)
#   CUSTOM_RX    cfgRxCount override (default: 256)
#   CUSTOM_SIZE  cfgSize override in bytes (default: 65536 = 64 KiB)
#   LARGE_SIZE   cfgSize for the large-frame cycle (default: 262144 = 256 KiB)
#   LARGE_FRAME  dmaLoopTest frame size for that cycle (default: 200000)
# ----------------------------------------------------------------------------

set -euo pipefail

DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"
DATADEV_KO="${DATADEV_KO:-data_dev/driver/datadev.ko}"
CUSTOM_TX="${CUSTOM_TX:-256}"
CUSTOM_RX="${CUSTOM_RX:-256}"
CUSTOM_SIZE="${CUSTOM_SIZE:-65536}"
LARGE_SIZE="${LARGE_SIZE:-262144}"
LARGE_FRAME="${LARGE_FRAME:-200000}"
INSMOD_TIMEOUT_SEC="${INSMOD_TIMEOUT_SEC:-120}"
PROC="/proc/datadev_0"

echo "=== Module parameter validation test ==="
echo "DATADEV_KO=$DATADEV_KO"
echo "CUSTOM_TX=$CUSTOM_TX CUSTOM_RX=$CUSTOM_RX CUSTOM_SIZE=$CUSTOM_SIZE"

# Require root/sudo capability. Skip gracefully if neither is available so
# this script can be dry-run on unprivileged hosts without breaking CI.
if [ "$(id -u)" -ne 0 ] && ! sudo -n true 2>/dev/null; then
   echo "SKIP: test_params.sh requires root/sudo (no TTY sudo allowed)"
   exit 0
fi

# sudo prefix helper -- empty when already root.
if [ "$(id -u)" -eq 0 ]; then
   SUDO=""
else
   SUDO="sudo"
fi

# Sanity check module file.
if [ ! -f "$DATADEV_KO" ]; then
   echo "FAIL: module file not found: $DATADEV_KO"
   exit 1
fi

# Unload datadev if already loaded. Ignore errors (maybe not loaded).
$SUDO rmmod datadev 2>/dev/null || true

# Wait for the /dev node to disappear (up to ~7.5 seconds).
for _ in $(seq 1 15); do
   [ ! -e "$DEV" ] && break
   sleep 0.5
done

# Reload with custom parameters.
echo "Loading datadev with cfgTxCount=$CUSTOM_TX cfgRxCount=$CUSTOM_RX cfgSize=$CUSTOM_SIZE"
timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" cfgTxCount="$CUSTOM_TX" cfgRxCount="$CUSTOM_RX" cfgSize="$CUSTOM_SIZE"

# Wait for /dev node to reappear.
for _ in $(seq 1 15); do
   [ -e "$DEV" ] && break
   sleep 0.5
done
if [ ! -e "$DEV" ]; then
   echo "FAIL: $DEV did not appear after reload"
   exit 1
fi
$SUDO chmod 666 "$DEV"

# Give the emulator's init busy-poll time to capture the first free buffer.
sleep 15

# Verify /proc exists before parsing.
if [ ! -e "$PROC" ]; then
   echo "FAIL: $PROC not found after reload"
   exit 1
fi

ERRORS=0

# Helper: identical semantics to test_proc.sh check_field, reads $PROC
# directly since contents don't change meaningfully between calls.
check_field() {
   local field="$1"
   local expected="$2"
   local value
   # `|| true` guards the whole pipeline: under `set -euo pipefail`, grep
   # returns 1 when the field is absent and `pipefail` would propagate that
   # to abort the script before check_field can report a clean FAIL. We want
   # the missing-field case to surface as a FAIL line and keep execution
   # going so subsequent check_field calls still run. Matches the pattern
   # already used on line 128 for the TX_LINE capture.
   value=$(grep -E "^[[:space:]]*${field}[[:space:]]*:" "$PROC" | head -1 | awk -F: '{print $2}' | tr -d '[:space:]' || true)
   if [ "$value" = "$expected" ]; then
      echo "PASS: $field = $value"
   else
      echo "FAIL: $field = '$value', expected '$expected'"
      ERRORS=$((ERRORS + 1))
   fi
}

# First "Buffer Count" in /proc is the RX section (Read Buffers).
check_field "Buffer Count" "$CUSTOM_RX"
# Buffer Size is the same for RX/TX (both use cfgSize).
check_field "Buffer Size"  "$CUSTOM_SIZE"

# Last "Buffer Count" line is the TX section (Write Buffers). Extract via tail.
TX_LINE=$(grep -E "^[[:space:]]*Buffer Count[[:space:]]*:" "$PROC" | tail -1 || true)
TX_COUNT=$(echo "$TX_LINE" | awk -F: '{print $NF}' | tr -d '[:space:]')
if [ "$TX_COUNT" = "$CUSTOM_TX" ]; then
   echo "PASS: TX Buffer Count = $TX_COUNT"
else
   echo "FAIL: TX Buffer Count = '$TX_COUNT', expected '$CUSTOM_TX'"
   ERRORS=$((ERRORS + 1))
fi

# --- Zero buffer count load/unload cycles ---
# dmaAllocBuffers() returns early with list->indexed == NULL when the
# requested count is zero, while list->subCount is already 1. dmaFreeBuffers()
# must skip the indexed walk in that case or rmmod oopses in Dma_Clean.
# Load with each count forced to zero and verify the module unloads cleanly.
zero_count_cycle() {
   local tx="$1"
   local rx="$2"
   echo "Zero-count cycle: cfgTxCount=$tx cfgRxCount=$rx"

   $SUDO rmmod datadev 2>/dev/null || true
   for _ in $(seq 1 15); do
      [ ! -e /sys/module/datadev ] && break
      sleep 0.5
   done

   if ! timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" \
         cfgTxCount="$tx" cfgRxCount="$rx" cfgSize="$CUSTOM_SIZE"; then
      echo "FAIL: insmod with cfgTxCount=$tx cfgRxCount=$rx failed"
      ERRORS=$((ERRORS + 1))
      return
   fi
   timeout 15 bash -c 'until [ "$(cat /sys/module/datadev/initstate 2>/dev/null)" = live ]; do sleep 0.5; done' || true

   # /proc must still be readable with an empty buffer list.
   if ! cat "$PROC" > /dev/null; then
      echo "FAIL: $PROC not readable with cfgTxCount=$tx cfgRxCount=$rx"
      ERRORS=$((ERRORS + 1))
   fi

   # An oops in dmaFreeBuffers kills the rmmod process and leaves the module
   # stuck, so a failed or timed-out rmmod is the regression signal here.
   if timeout --kill-after=5s 30s $SUDO rmmod datadev; then
      echo "PASS: load/unload with cfgTxCount=$tx cfgRxCount=$rx"
   else
      echo "FAIL: rmmod after cfgTxCount=$tx cfgRxCount=$rx"
      ERRORS=$((ERRORS + 1))
   fi
}

zero_count_cycle 0 "$CUSTOM_RX"
zero_count_cycle "$CUSTOM_TX" 0

# --- Invalid cfgMode must fail the module load ---
# cfgMode=4 (BUFF_ARM_ACP) is not supported on PCIe.
echo "Invalid cfgMode cycle: cfgMode=4 (expect insmod failure)"
$SUDO rmmod datadev 2>/dev/null || true
for _ in $(seq 1 15); do
   [ ! -e /sys/module/datadev ] && break
   sleep 0.5
done

if timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" cfgMode=4 2>/dev/null; then
   echo "FAIL: insmod with cfgMode=4 unexpectedly succeeded"
   ERRORS=$((ERRORS + 1))
   $SUDO rmmod datadev 2>/dev/null || true
else
   echo "PASS: insmod with cfgMode=4 rejected"
fi

if grep -q '^datadev ' /proc/modules; then
   echo "FAIL: datadev still listed in /proc/modules after rejected cfgMode=4 load"
   ERRORS=$((ERRORS + 1))
   $SUDO rmmod datadev 2>/dev/null || true
else
   echo "PASS: datadev not loaded after rejected cfgMode=4 load"
fi

# --- Large-frame loopback above the emulator's 128 KiB reset maxSize ---
# AxisG2_Init writes cfgSize into the maxSize register (offset 0x0028). The
# emulator's TX path must bound frames by that live value, not by its reset
# constant EMU_REG_MAXSIZE_INIT (0x20000). With the constant, every frame
# above 128 KiB is dropped: the TX buffer returns via the read ring with a
# size=0 completion but no RX completion is written, so dmaLoopTest reports
# no error while RxCount stays at zero. Reload with cfgSize=LARGE_SIZE and
# loop a LARGE_FRAME-byte frame; require at least one RX completion.
# Buffer counts are reduced to 64 so the coherent allocation footprint stays
# at 32 MiB, the same as the 256 x 64 KiB configuration above.
large_frame_cycle() {
   local out
   echo "Large-frame cycle: cfgSize=$LARGE_SIZE frame=$LARGE_FRAME"

   if [ ! -x "$APP_BIN/dmaLoopTest" ]; then
      echo "FAIL: $APP_BIN/dmaLoopTest not found"
      ERRORS=$((ERRORS + 1))
      return
   fi

   $SUDO rmmod datadev 2>/dev/null || true
   for _ in $(seq 1 15); do
      [ ! -e /sys/module/datadev ] && break
      sleep 0.5
   done

   if ! timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" \
         cfgTxCount=64 cfgRxCount=64 cfgSize="$LARGE_SIZE"; then
      echo "FAIL: insmod with cfgSize=$LARGE_SIZE failed"
      ERRORS=$((ERRORS + 1))
      return
   fi
   timeout 15 bash -c 'until [ "$(cat /sys/module/datadev/initstate 2>/dev/null)" = live ]; do sleep 0.5; done' || true
   for _ in $(seq 1 15); do
      [ -e "$DEV" ] && break
      sleep 0.5
   done
   if [ ! -e "$DEV" ]; then
      echo "FAIL: $DEV did not appear after cfgSize=$LARGE_SIZE reload"
      ERRORS=$((ERRORS + 1))
      return
   fi
   $SUDO chmod 666 "$DEV"
   sleep 2

   out=$(mktemp)
   # timeout rc=124 is the normal exit path; dmaLoopTest runs until killed.
   timeout 10 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$LARGE_FRAME" > "$out" 2>&1 || true

   # dmaLoopTest exits 0 even when a worker hit Read Error / Write Error /
   # Error opening device, so grep the log like the other tests/ cells do.
   if grep -qE "Read Error|Write Error|Error opening device|Prbs mismatch" "$out"; then
      echo "FAIL: dmaLoopTest worker error at size=$LARGE_FRAME (cfgSize=$LARGE_SIZE)"
      cat "$out"
      ERRORS=$((ERRORS + 1))
      rm -f "$out"
      return
   fi

   # Last RxCount stats line; dest 0 is the first (only) column.
   local rx_count
   rx_count=$(grep -E '^RxCount:' "$out" | tail -1 | awk '{print $2}' || true)
   if [ -n "$rx_count" ] && [ "$rx_count" -gt 0 ] 2>/dev/null; then
      echo "PASS: large-frame loopback size=$LARGE_FRAME cfgSize=$LARGE_SIZE (RxCount=$rx_count)"
   else
      echo "FAIL: no RX completions for size=$LARGE_FRAME with cfgSize=$LARGE_SIZE (RxCount='$rx_count')"
      cat "$out"
      $SUDO dmesg | grep -i "exceeds maxSize" | tail -5 || true
      ERRORS=$((ERRORS + 1))
   fi
   rm -f "$out"
}

large_frame_cycle

# --- Cleanup: restore default parameters ---
echo "Restoring default module configuration (cfgDebug=1)"
$SUDO rmmod datadev 2>/dev/null || true
# Wait briefly for cleanup.
for _ in $(seq 1 15); do
   [ ! -e "$DEV" ] && break
   sleep 0.5
done
timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" cfgDebug=1 2>/dev/null || \
   echo "WARN: could not reload datadev with defaults"
for _ in $(seq 1 15); do
   [ -e "$DEV" ] && break
   sleep 0.5
done
$SUDO chmod 666 "$DEV" 2>/dev/null || true

echo "=== params test: $ERRORS errors ==="
exit "$ERRORS"
