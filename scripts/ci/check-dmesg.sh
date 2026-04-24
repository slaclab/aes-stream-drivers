#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Check dmesg for errors using a baseline-delta model. Extracts the delta
#    after the load-modules marker and fails on oops/panic/BUG/WARNING. If
#    the marker is missing but driver modules are loaded, falls back to a
#    full-ring scan so a /dev/kmsg write failure cannot silently mask a
#    driver-induced kernel error.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Finds the baseline marker injected by load-modules-*.sh, extracts the delta
# (lines AFTER the marker = "driver-induced"), and fails on any oops/panic/
# BUG/WARNING in that delta. Benign kernel-cmdline echoes (drm panic, panic=,
# panic_on_oops, panic_on_warn) are excluded.
#
# If the marker is missing, the gate distinguishes two cases:
#   - load-modules-*.sh was never reached (genuine build-only cell) -> skip
#     with exit 0. Indicator: /tmp/ci_load_attempted absent AND no
#     /sys/module/datadev[_emulator]|nvidia_p2p_stub entries.
#   - load-modules-*.sh ran but /dev/kmsg write dropped silently (permissions,
#     kmsg throttling, ring-buffer wrap) -> fall back to full-ring scan so
#     driver-induced oops/panic/BUG/WARNING cannot pass unnoticed. Indicator:
#     /tmp/ci_load_attempted present (durable across unload-modules), or
#     module dirs still under /sys/module (covers manual/local-CI usage where
#     check-dmesg.sh is invoked while modules are still resident). Mirrors the
#     empty-marker fallback in tests/vm_inside.sh.
#
# Exit codes: 0=clean or skipped, 1=driver-induced errors found
# ----------------------------------------------------------------------------

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo_step() { echo -e "${GREEN}==>${NC} $1"; }
echo_warn() { echo -e "${YELLOW}WARN:${NC} $1"; }
echo_fail() { echo -e "${RED}FAIL:${NC} $1"; }

# Detect if we need sudo
if [ "$(id -u)" -eq 0 ]; then
   SUDO=""
else
   SUDO="sudo"
fi

echo_step "Checking dmesg for errors (baseline-delta model)"

# --- Locate baseline marker ------------------------------------------------
# load-modules-*.sh writes the per-run marker token to /tmp/ci_dmesg_marker
# and also injects it into the kernel ring buffer. Matches the /tmp/ci_*
# side-channel convention used by install-deps.sh (/tmp/ci_kver, /tmp/ci_host_match).
MARKER_FILE="/tmp/ci_dmesg_marker"
MARKER=""
if [ -r "$MARKER_FILE" ]; then
   MARKER="$(cat "$MARKER_FILE" 2>/dev/null || true)"
fi

MARKER_OK=0
if [ -n "$MARKER" ] && $SUDO dmesg | grep -qF "$MARKER"; then
   MARKER_OK=1
fi

if [ "$MARKER_OK" -eq 0 ]; then
   # Marker missing. Distinguish "load-modules never ran" (genuine build-only)
   # from "load-modules ran but kmsg write dropped" (false-PASS hazard).
   #   /tmp/ci_load_attempted is dropped by load-modules-*.sh BEFORE the kmsg
   #     attempt and survives unload-modules-*.sh, so it remains visible to
   #     check-dmesg.sh in the workflow's `if: always()` slot.
   #   /sys/module/<m> presence is a secondary signal for manual/local-CI
   #     usage where check-dmesg.sh is invoked while modules are still loaded.
   LOAD_ATTEMPTED=0
   if [ -f /tmp/ci_load_attempted ]; then
      LOAD_ATTEMPTED=1
   fi
   if [ "$LOAD_ATTEMPTED" -eq 0 ]; then
      for m in datadev datadev_emulator nvidia_p2p_stub; do
         if [ -d "/sys/module/$m" ]; then
            LOAD_ATTEMPTED=1
            break
         fi
      done
   fi

   if [ "$LOAD_ATTEMPTED" -eq 0 ]; then
      echo_warn "No baseline marker and no evidence of module load — build-only cell"
      echo "=== dmesg (last 50 lines) for visibility ==="
      $SUDO dmesg | tail -50 || true
      echo_step "Dmesg gate skipped (build-only cell)"
      exit 0
   fi

   echo_warn "No baseline marker but load-modules ran (/tmp/ci_load_attempted present or modules resident) — /dev/kmsg write likely dropped; falling back to full-ring scan"
fi

# --- Extract delta ---------------------------------------------------------
# awk: once we see the marker line, print everything AFTER it. Using index()
# for literal-string match so UUID / dash characters cannot be misread as regex
# metacharacters. With an empty marker (full-ring fallback), index($0,"")
# returns 1 on every line, so the awk filter scans the entire ring buffer.
if [ "$MARKER_OK" -eq 1 ]; then
   DELTA="$($SUDO dmesg | awk -v m="$MARKER" 'f{print} index($0,m){f=1}')"
   echo "=== Delta (dmesg since baseline marker) ==="
else
   DELTA="$($SUDO dmesg)"
   echo "=== Delta (full dmesg ring — marker fallback) ==="
fi
echo "$DELTA" | tail -100
echo "==========================================="

# --- Benign exclusions (retained from commit 8fa7049) ----------------------
# Kernel-cmdline echoes / DRM plane registration messages that can appear
# post-load but are not driver-induced. Keep the exact set from the current
# (pre-rewrite) check-dmesg.sh so behavior is identical for benign matches.
BENIGN_PATTERN='drm panic|panic=|panic_on_oops|panic_on_warn'

# --- Oops / panic / BUG check ---------------------------------------------
if echo "$DELTA" | grep -iE 'oops|panic|BUG:' | grep -viE "$BENIGN_PATTERN"; then
   echo_fail "Driver-induced kernel error detected in delta"
   exit 1
fi

# --- WARNING check (no more allowlist; delta is post-load by construction) -
# The previous allowlist is deleted: anything it was meant to pass is already
# inside the delta by construction, and a real driver WARNING in the delta
# must fail the gate.
if echo "$DELTA" | grep -iE 'WARNING:'; then
   echo_fail "Driver-induced kernel warning detected in delta"
   exit 1
fi

# --- Verify ISR was exercised (informational, reads from delta) -----------
if echo "$DELTA" | grep -q "Irq: Called"; then
   echo "PASS: ISR invocation confirmed (interrupt-driven path)"
else
   echo "INFO: No ISR invocation found in delta (debug logging may be off)"
fi

# --- Emulator DMA statistics (from delta) --------------------------------
echo "=== Emulator DMA Statistics (delta) ==="
echo "$DELTA" | grep "emu:" | tail -20 || echo "No emulator stats found"

echo_step "dmesg gate passed"
