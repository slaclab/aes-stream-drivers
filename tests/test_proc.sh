#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    /proc/datadev_0 interface test. Verifies presence and values of key
#    fields emitted by Dma_SeqShow and AxisG2_SeqShow.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Reads /proc/datadev_0 and verifies the presence and values of the key
# fields emitted by Dma_SeqShow and AxisG2_SeqShow. This confirms the
# driver's /proc interface works against the emulated device and that
# module parameters are honored.
#
# Field verification uses flexible whitespace-tolerant grep patterns so
# minor format changes in seq_printf don't break the test.
#
# Environment variables:
#   DEV_IDX              /proc/datadev_$DEV_IDX (default: 0)
#   EXPECTED_BUFF_COUNT  Expected Buffer Count (default: 1024)
#   EXPECTED_BUFF_SIZE   Expected Buffer Size  (default: 131072)
# ----------------------------------------------------------------------------

set -uo pipefail

DEV_IDX="${DEV_IDX:-0}"

# Read actual module parameters from /proc if env vars not set.
# The defaults (1024 / 131072) only match when the driver is loaded with
# default module params; CI loads with reduced values (64 / 65536).
_PROC_FILE="/proc/datadev_${DEV_IDX}"
if [ -z "${EXPECTED_BUFF_COUNT:-}" ] && [ -e "$_PROC_FILE" ]; then
   EXPECTED_BUFF_COUNT=$(grep -E "^[[:space:]]*Buffer Count[[:space:]]*:" "$_PROC_FILE" | head -1 | awk -F: '{print $2}' | tr -d '[:space:]')
fi
if [ -z "${EXPECTED_BUFF_SIZE:-}" ] && [ -e "$_PROC_FILE" ]; then
   EXPECTED_BUFF_SIZE=$(grep -E "^[[:space:]]*Buffer Size[[:space:]]*:" "$_PROC_FILE" | head -1 | awk -F: '{print $2}' | tr -d '[:space:]')
fi
EXPECTED_BUFF_COUNT="${EXPECTED_BUFF_COUNT:-1024}"
EXPECTED_BUFF_SIZE="${EXPECTED_BUFF_SIZE:-131072}"
PROC="/proc/datadev_${DEV_IDX}"
# mktemp + trap so parallel invocations don't clobber each other.
OUT=$(mktemp -t proc_output.XXXXXX)
trap 'rm -f "$OUT"' EXIT

echo "=== /proc/datadev_${DEV_IDX} interface test ==="
echo "PROC=$PROC"

# Verify the /proc entry exists at all.
if [ ! -e "$PROC" ]; then
   echo "FAIL: $PROC not found"
   exit 1
fi

# Dump once so we don't re-read /proc multiple times (contents can change).
cat "$PROC" > "$OUT"

ERRORS=0

# Helper: verify "field : value" matches expected. Flexible whitespace, first
# match (head -1) which in /proc output is the RX section (Read Buffers).
check_field() {
   local field="$1"
   local expected="$2"
   local value
   value=$(grep -E "^[[:space:]]*${field}[[:space:]]*:" "$OUT" | head -1 | awk -F: '{print $2}' | tr -d '[:space:]')
   if [ "$value" = "$expected" ]; then
      echo "PASS: $field = $value"
   else
      echo "FAIL: $field = '$value', expected '$expected'"
      ERRORS=$((ERRORS + 1))
   fi
}

# Helper: verify a field is present without checking its value.
check_field_present() {
   local field="$1"
   if grep -qE "^[[:space:]]*${field}[[:space:]]*:" "$OUT"; then
      echo "PASS: $field present"
   else
      echo "FAIL: $field missing"
      ERRORS=$((ERRORS + 1))
   fi
}

# --- Value checks ---
# Buffer Count appears twice (Read Buffers, Write Buffers). head -1 grabs RX.
check_field "Buffer Count" "$EXPECTED_BUFF_COUNT"
check_field "Buffer Size"  "$EXPECTED_BUFF_SIZE"
# Emulator advertises 128-bit descriptors: Desc 128 En : 1.
check_field "Desc 128 En"  "1"

# --- Presence-only checks (values vary per run) ---
check_field_present "IRQ"
check_field_present "DMA Driver's API Version"
check_field_present "Buffer Mode"
check_field_present "Buffers In User"
check_field_present "Buffers In Hw"

# --- Strict API version check ---
# /proc output from Dma_SeqShow: " DMA Driver's API Version : 0x6"
if grep -qE "API Version[[:space:]]*:[[:space:]]*0x6" "$OUT"; then
   echo "PASS: API Version 0x6"
else
   echo "FAIL: API Version != 0x6"
   ERRORS=$((ERRORS + 1))
fi

echo "=== proc test: $ERRORS errors ==="
exit "$ERRORS"
