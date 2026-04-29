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
# ----------------------------------------------------------------------------

set -euo pipefail

DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"
DATADEV_KO="${DATADEV_KO:-data_dev/driver/datadev.ko}"
CUSTOM_TX="${CUSTOM_TX:-256}"
CUSTOM_RX="${CUSTOM_RX:-256}"
CUSTOM_SIZE="${CUSTOM_SIZE:-65536}"
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

# Give the DMA engine time to complete the emulator seed phase.
# With 256+ buffers the emulator busy-poll and seed phase take longer
# than the default 64-buffer configuration.
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
