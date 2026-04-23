#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Check dmesg for errors using a baseline-delta model. Extracts the delta
#    after the load-modules marker and fails on oops/panic/BUG/WARNING.
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
# If no marker is found (build-only cells, or load-modules aborted before the
# marker line), skip the gate with exit 0 and print a dmesg tail for visibility.
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

if [ -z "$MARKER" ] || ! $SUDO dmesg | grep -qF "$MARKER"; then
   echo_warn "No baseline marker found in dmesg — no driver was loaded in this cell"
   echo "=== dmesg (last 50 lines) for visibility ==="
   $SUDO dmesg | tail -50 || true
   echo_step "Dmesg gate skipped (build-only cell)"
   exit 0
fi

# --- Extract delta ---------------------------------------------------------
# awk: once we see the marker line, print everything AFTER it. Using index()
# for literal-string match so UUID / dash characters cannot be misread as regex
# metacharacters.
DELTA="$($SUDO dmesg | awk -v m="$MARKER" 'f{print} index($0,m){f=1}')"

echo "=== Delta (dmesg since baseline marker) ==="
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
