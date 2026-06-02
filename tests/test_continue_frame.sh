#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    DMA "continue" feature test. Reloads the emulator with a small
#    emu_max_transfer so each test frame is split across multiple DMA buffers
#    (cont=1 on all but the last), then runs dmaContinueTest to verify the
#    reassembled payload passes PRBS. Asserts the driver's Continue Count
#    advanced (proving the split path ran) and that dmesg stays clean.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Reproduces the real-hardware symptom (PRBS passes at <= max-transfer but
# fails just over it) in CI: the AxiStreamDmaV2 firmware splits any frame
# larger than its internal max-transfer (512 KiB on the C1100) into continued
# segments that user space must stitch back together. The emulator models
# that split via emu_max_transfer; this test drives frames at exactly the
# boundary (1 segment), just over it (2 segments), and several times over it,
# checking integrity at each size.
#
# Requires root/sudo -- this test reloads kernel modules. Only meaningful
# against the emulator (the split is firmware-driven on real hardware), so it
# SKIPs when the emulator .ko is absent.
#
# Environment variables:
#   DEV              Device path (default: /dev/datadev_0)
#   APP_BIN          Binary directory (default: data_dev/app/bin)
#   DATADEV_KO       Path to datadev.ko (default: data_dev/driver/datadev.ko)
#   EMULATOR_KO      Path to datadev_emulator.ko
#   EMU_MAX_TRANSFER Per-descriptor split threshold in bytes (default: 16384)
#   CFG_SIZE         datadev cfgSize / DMA buffer size (default: 65536)
#   COUNT            Frames per size (default: 100)
#   TIMEOUT_SEC      Module init timeout (default: 15)
# ----------------------------------------------------------------------------

set -uo pipefail

DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"
DATADEV_KO="${DATADEV_KO:-data_dev/driver/datadev.ko}"
EMULATOR_KO="${EMULATOR_KO:-emulator/driver/datadev_emulator.ko}"
EMU_MAX_TRANSFER="${EMU_MAX_TRANSFER:-16384}"
CFG_SIZE="${CFG_SIZE:-65536}"
COUNT="${COUNT:-100}"
TIMEOUT_SEC="${TIMEOUT_SEC:-15}"
INSMOD_TIMEOUT_SEC="${INSMOD_TIMEOUT_SEC:-120}"

DEV_IDX="${DEV##*_}"
case "$DEV_IDX" in (*[!0-9]*|'') DEV_IDX=0 ;; esac
PROC="/proc/datadev_${DEV_IDX}"

# Require root/sudo. Skip gracefully on unprivileged hosts (mirrors
# test_irq_modes.sh) so the suite stays green on dry-run boxes.
if [ "$(id -u)" -ne 0 ] && ! sudo -n true 2>/dev/null; then
   echo "SKIP: test_continue_frame.sh requires root/sudo (no TTY sudo allowed)"
   exit 0
fi
if [ "$(id -u)" -eq 0 ]; then SUDO=""; else SUDO="sudo"; fi

# The split is firmware-driven on real hardware; this CI test models it with
# the emulator. Without the emulator .ko there is nothing to drive the split.
if [ ! -f "$EMULATOR_KO" ]; then
   echo "SKIP: test_continue_frame.sh requires the emulator ($EMULATOR_KO not found)"
   exit 0
fi
if [ ! -f "$DATADEV_KO" ]; then
   echo "FAIL: module file not found: $DATADEV_KO"
   exit 1
fi

# CFG_SIZE must be large enough to hold the largest test frame in one TX
# buffer (the emulator does the RX-side split), and EMU_MAX_TRANSFER must be
# smaller than CFG_SIZE so frames actually cross the boundary.
if [ "$EMU_MAX_TRANSFER" -ge "$CFG_SIZE" ]; then
   echo "FAIL: EMU_MAX_TRANSFER ($EMU_MAX_TRANSFER) must be < CFG_SIZE ($CFG_SIZE)"
   exit 1
fi

FAILED=0
echo "=== DMA continue-frame test (emu_max_transfer=$EMU_MAX_TRANSFER, cfgSize=$CFG_SIZE) ==="

ensure_node() {
   if [ ! -e "$DEV" ]; then
      local major
      major=$(awk -v n="datadev_${DEV_IDX}" '$2 == n { print $1 }' /proc/devices)
      [ -n "$major" ] && $SUDO mknod "$DEV" c "$major" "$DEV_IDX"
   fi
   $SUDO chmod 666 "$DEV" 2>/dev/null || true
}

# Reload the emulator + datadev with the given emu_max_transfer. Empty arg
# restores the emulator default (no split for in-spec frames).
load_stack() {
   local max_transfer="$1"
   local emu_params=""
   [ -n "$max_transfer" ] && emu_params="emu_max_transfer=$max_transfer"

   $SUDO rmmod datadev 2>/dev/null || true
   $SUDO rmmod datadev_emulator 2>/dev/null || true
   for _ in $(seq 1 15); do [ ! -e "$DEV" ] && break; sleep 0.5; done

   # shellcheck disable=SC2086
   if ! timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" \
         $SUDO insmod "$EMULATOR_KO" $emu_params; then
      echo "[FAIL] could not load emulator ($emu_params)"
      return 1
   fi
   if ! timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" \
         $SUDO insmod "$DATADEV_KO" cfgTxCount=64 cfgRxCount=64 cfgSize="$CFG_SIZE"; then
      echo "[FAIL] could not load datadev (cfgSize=$CFG_SIZE)"
      return 1
   fi
   timeout "$TIMEOUT_SEC" bash -c \
      "until [ \"\$(cat /sys/module/datadev/initstate 2>/dev/null)\" = live ]; do sleep 0.5; done" || {
      echo "[FAIL] datadev did not reach live state"
      return 1
   }
   ensure_node
   sleep 1
   return 0
}

# Always restore the default (no-split) emulator + standard datadev so later
# suite tests see normal config, regardless of how this test exits.
restore_default() {
   load_stack "" >/dev/null 2>&1 || true
}
trap restore_default EXIT

read_continue_count() {
   [ -r "$PROC" ] || { echo 0; return; }
   awk -F: '/Continue Count/ { gsub(/[^0-9]/,"",$2); print $2; exit }' "$PROC"
}

if ! load_stack "$EMU_MAX_TRANSFER"; then
   exit 1
fi

DMESG_BEFORE=$($SUDO dmesg | wc -l)
CC_BEFORE=$(read_continue_count)

# Frame sizes: exactly at the boundary (1 segment, cont=0), just over it
# (2 segments -- the "512 KiB + 64" analog), and several segments.
SIZES="$EMU_MAX_TRANSFER $((EMU_MAX_TRANSFER + 64)) $CFG_SIZE"

for SZ in $SIZES; do
   echo "--- continue sub-test: size=$SZ bytes ---"
   TMPFILE=$(mktemp)
   timeout 60 "$APP_BIN/dmaContinueTest" -p "$DEV" -m 0 -s "$SZ" -c "$COUNT" > "$TMPFILE" 2>&1
   RC=$?

   if grep -qE "Read Error|Write Error|Error opening device|Prbs mismatch" "$TMPFILE"; then
      echo "[FAIL] continue_frame size=$SZ -- error reported"
      grep -E "Read Error|Write Error|Error opening device|Prbs mismatch" "$TMPFILE" | head -5
      FAILED=$((FAILED + 1))
   elif [ "$RC" -ne 0 ] || ! grep -q "=== dmaContinueTest: PASS ===" "$TMPFILE"; then
      echo "[FAIL] continue_frame size=$SZ -- dmaContinueTest did not pass (rc=$RC)"
      grep -E "TxCount|RxCount|PrbErr|MaxSegsPerFrame|FAIL" "$TMPFILE" | head -10
      FAILED=$((FAILED + 1))
   else
      echo "[PASS] continue_frame size=$SZ"
      grep "MaxSegsPerFrame" "$TMPFILE" || true
   fi
   rm -f "$TMPFILE"
done

# Prove the split path actually ran: contCount must have advanced (the
# >boundary sizes emit cont=1 segments). A flat counter means the frames were
# never split, so the test would be vacuously green.
CC_AFTER=$(read_continue_count)
if [ -r "$PROC" ]; then
   if [ "${CC_AFTER:-0}" -le "${CC_BEFORE:-0}" ]; then
      echo "[FAIL] continue_frame -- Continue Count did not advance ($CC_BEFORE -> $CC_AFTER); split never exercised"
      FAILED=$((FAILED + 1))
   else
      echo "[INFO] Continue Count advanced $CC_BEFORE -> $CC_AFTER"
   fi
else
   echo "[INFO] $PROC not readable; skipping Continue Count assertion"
fi

# Kernel health: scan the dmesg delta produced by this test.
DMESG_DELTA=$($SUDO dmesg | tail -n "+$((DMESG_BEFORE + 1))")
if printf '%s\n' "$DMESG_DELTA" | grep -iE 'oops|panic|BUG:|WARNING:'; then
   echo "[FAIL] continue_frame -- kernel error in dmesg"
   FAILED=$((FAILED + 1))
fi

if [ "$FAILED" -eq 0 ]; then
   echo "PASS: continue feature verified across boundary sizes"
else
   echo "FAIL: $FAILED continue-frame check(s) failed"
fi

exit "$FAILED"
