#!/bin/bash
# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description:
#     End-to-end stub-owned address handoff verifier. Runs the five
#     success criteria. Invoked by scripts/ci/test-gpu.sh (or ad-hoc) AFTER
#     load-modules-gpu.sh has brought up the three-module stack.
#
#     Exit codes: 0=all five SCs PASS, 1=at least one SC FAIL,
#                 5=precondition failure (e.g. modules not loaded before invocation).
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------

set -uo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo_pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
echo_fail() { echo -e "${RED}[FAIL]${NC} $1"; }
echo_step() { echo -e "${YELLOW}==>${NC} $1"; }

# Detect if we need sudo
if [ "$(id -u)" -eq 0 ]; then
   SUDO=""
else
   SUDO="sudo"
fi

# Preconditions: all three modules must be loaded. If any is missing,
# the caller (load-modules-gpu.sh) failed — return 5, not 1.
for m in datadev_emulator nvidia_p2p_stub datadev; do
  if ! lsmod | awk '{print $1}' | grep -qx "$m"; then
    echo_fail "precondition: module $m not loaded"
    exit 5
  fi
done

# ----------------------------------------------------------------------------
# SC1 — emu_gpu_addr_lookup exported GPL + /dev/nvidia_p2p_stub_mem usable
# ----------------------------------------------------------------------------
echo_step "emu_gpu_addr_lookup exported GPL + /dev/nvidia_p2p_stub_mem usable"
SC1=FAIL
if ! $SUDO grep -qE "T emu_gpu_addr_lookup\s+\[nvidia_p2p_stub\]" /proc/kallsyms; then
  echo_fail "SC1.a: emu_gpu_addr_lookup not found as T in [nvidia_p2p_stub]"
elif [ ! -e /dev/nvidia_p2p_stub_mem ]; then
  echo_fail "SC1.b: /dev/nvidia_p2p_stub_mem not present"
elif ! $SUDO ./emulator/driver/tests/stub_mmap_test; then
  echo_fail "SC1.c: stub_mmap_test exited non-zero"
else
  echo_pass "SC1"
  SC1=PASS
fi

# ----------------------------------------------------------------------------
# SC2 — no 'non-contiguous GPU memory detected' warning in dmesg
# ----------------------------------------------------------------------------
echo_step "no 'non-contiguous GPU memory detected' in dmesg"
SC2=FAIL
# Use the baseline marker from load-modules-gpu.sh if present, else tail.
MARKER_FILE=/tmp/ci_dmesg_marker
if [ -f "$MARKER_FILE" ] && MARKER=$(cat "$MARKER_FILE") && \
   $SUDO dmesg | awk -v m="$MARKER" '$0 ~ m {on=1} on{print}' | \
        grep -q "non-contiguous GPU memory detected"; then
  echo_fail "non-contiguous warning found after marker"
elif [ ! -f "$MARKER_FILE" ] && $SUDO dmesg | tail -200 | \
        grep -q "non-contiguous GPU memory detected"; then
  echo_fail "non-contiguous warning found in last 200 lines"
else
  echo_pass "SC2"
  SC2=PASS
fi

# ----------------------------------------------------------------------------
# SC3 — pr_info_once 'gpu addr_lookup ... (first hit)' probe in dmesg
# ----------------------------------------------------------------------------
echo_step "pr_info_once 'gpu addr_lookup ... (first hit)' in dmesg"
SC3=FAIL
if $SUDO dmesg | grep -qE "gpu addr_lookup fake=0x[0-9a-f]+ kva=0x[0-9a-f]+ \(first hit\)"; then
  echo_pass "SC3"
  SC3=PASS
else
  echo_fail "first-hit probe not found in dmesg (was Gpu_AddNvidia called?)"
fi

# ----------------------------------------------------------------------------
# SC4 — rmmod datadev && rmmod datadev_emulator && rmmod nvidia_p2p_stub
# (Natural reverse-of-load order. The stub's EXPORT_SYMBOL_GPL surface is consumed EAGERLY by the emulator via
# emu_gpu_register_drain_cb at module_init, so the stub must outlive the
# emulator. The drain callback governs the RUNTIME have_pin release on an
# empty address table, not unload sequencing.)
# ----------------------------------------------------------------------------
echo_step "rmmod datadev && rmmod datadev_emulator && rmmod nvidia_p2p_stub"
SC4=FAIL
# Stash dmesg length before rmmod so we can grep the delta for 'Module in use'.
DMESG_PRE=$($SUDO dmesg | wc -l)
if ! $SUDO rmmod datadev 2>&1; then
  echo_fail "SC4.a: rmmod datadev failed"
elif ! $SUDO rmmod datadev_emulator 2>&1; then
  echo_fail "SC4.b: rmmod datadev_emulator failed"
elif ! $SUDO rmmod nvidia_p2p_stub 2>&1; then
  echo_fail "SC4.c: rmmod nvidia_p2p_stub failed"
else
  if $SUDO dmesg | tail -n +$DMESG_PRE | grep -qE "Module in use|EWOULDBLOCK"; then
    echo_fail "SC4.d: 'Module in use' / EWOULDBLOCK in dmesg during rmmod sequence"
  else
    echo_pass "SC4"
    SC4=PASS
  fi
fi

# ----------------------------------------------------------------------------
# SC5 — git diff origin/main gpu_async.c files empty (regression gate)
# ----------------------------------------------------------------------------
echo_step "git diff origin/main -- gpu_async.c files empty (real-HW regression gate)"
SC5=FAIL
if git diff --exit-code origin/main -- common/driver/gpu_async.c data_dev/driver/src/gpu_async.c >/dev/null 2>&1; then
  echo_pass "SC5"
  SC5=PASS
else
  echo_fail "gpu_async.c modified since origin/main — real-HW regression risk:"
  git diff --stat origin/main -- common/driver/gpu_async.c data_dev/driver/src/gpu_async.c
fi

# ----------------------------------------------------------------------------
# Summary
# ----------------------------------------------------------------------------
PASS=0
for sc in "$SC1" "$SC2" "$SC3" "$SC4" "$SC5"; do
  [ "$sc" = "PASS" ] && PASS=$((PASS + 1))
done

echo ""
if [ $PASS -eq 5 ]; then
  echo -e "${GREEN}=== HANDOFF: 5/5 PASS ===${NC}"
  exit 0
else
  echo -e "${RED}=== HANDOFF: $PASS/5 PASS ===${NC}"
  $SUDO dmesg | tail -50
  exit 1
fi
