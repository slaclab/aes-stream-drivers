#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Datadev reload segfault reproducer. Tests for the NULL-pointer deref in
#    dmaAllocBuffers when datadev is rebound after BUFF_STREAM cycle.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Standalone reproducer for the NULL-pointer deref in
# dmaAllocBuffers+0x313/0x510 [datadev] that fires when datadev is rebound
# after a prior BUFF_STREAM insmod cycle while the emulator stays loaded.
#
# The bug: pdev->dev.cma_area on the emulator's virtual PCI device is
# cleared exactly once at emu_pci_host_create time but is re-populated
# (with garbage, observed: 0x726f662074736564) by a later kernel code
# path. The second datadev probe defaults to cfgMode=BUFF_COHERENT (1)
# and dereferences that stale pointer inside __cma_alloc+0x3c.
#
# Sequence:
#   1) insmod emulator (stays loaded throughout)
#   2) insmod datadev cfgMode=2 (BUFF_STREAM -- does NOT touch CMA)
#   3) rmmod datadev (emulator keeps the virtual pci_dev alive)
#   4) insmod datadev (defaults to BUFF_COHERENT -- hits dma_alloc_coherent)
#
# Expected:
#   - Pre-fix emulator:  GPF in __cma_alloc, /dev/datadev_0 never appears,
#                        script exits non-zero with dmesg dump.
#   - Fixed emulator:    step 4 succeeds, /dev/datadev_0 appears,
#                        script exits 0 with "PASS: ..." line.
#
# Requires root/sudo -- this test reloads kernel modules. Safe to run on
# unprivileged hosts: the script auto-SKIPs. Intended invocation is
# inside the CI VM spawned by scripts/ci-local/run_cell.sh.
#
# Environment variables:
#   DEV                 Device path         (default: /dev/datadev_0)
#   DATADEV_KO          Path to datadev.ko  (default: data_dev/driver/datadev.ko)
#   EMU_KO              Path to emulator.ko (default: emulator/driver/datadev_emulator.ko)
#   INSMOD_TIMEOUT_SEC  insmod hard timeout (default: 120)
# ----------------------------------------------------------------------------

set -euo pipefail

DEV="${DEV:-/dev/datadev_0}"
DATADEV_KO="${DATADEV_KO:-data_dev/driver/datadev.ko}"
EMU_KO="${EMU_KO:-emulator/driver/datadev_emulator.ko}"
INSMOD_TIMEOUT_SEC="${INSMOD_TIMEOUT_SEC:-120}"

echo "=== test_reload_segfault.sh ==="

# Require root/sudo capability. Skip gracefully if neither is available so
# this script can be dry-run on unprivileged hosts without breaking CI.
if [ "$(id -u)" -ne 0 ] && ! sudo -n true 2>/dev/null; then
   echo "SKIP: test_reload_segfault.sh requires root/sudo (no TTY sudo allowed)"
   exit 0
fi

if [ "$(id -u)" -eq 0 ]; then
   SUDO=""
else
   SUDO="sudo"
fi

# Guarantee driver/emulator teardown on any exit path. Under `set -e`, a
# failure in any phase would otherwise skip the in-script cleanup at the
# bottom and leave modules loaded for subsequent tests. Single-quoted so
# $SUDO expansion happens at trap-fire time (after SUDO is set above).
trap '$SUDO rmmod datadev 2>/dev/null || true; $SUDO rmmod datadev_emulator 2>/dev/null || true' EXIT

# Sanity-check that both modules have been built.
if [ ! -f "$DATADEV_KO" ]; then
   echo "FAIL: $DATADEV_KO not found -- run 'make' first"
   exit 1
fi
if [ ! -f "$EMU_KO" ]; then
   echo "FAIL: $EMU_KO not found -- run 'make' in emulator/driver first"
   exit 1
fi

# Clean slate. Ignore errors -- modules may not be loaded.
$SUDO rmmod datadev 2>/dev/null || true
$SUDO rmmod datadev_emulator 2>/dev/null || true
for _ in $(seq 1 15); do
   [ ! -e "$DEV" ] && break
   sleep 0.5
done

# Phase A: Load emulator. It creates the virtual pci_dev with cma_area=NULL.
# insmod hangs are possible when a prior test left the emulator in a bad
# state (stray kthread, pending work), so hard-bound via timeout like the
# other reload-heavy tests (test_params.sh, test_backpressure.sh).
echo "[A] insmod emulator"
timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$EMU_KO"

# Phase B: First insmod with cfgMode=2 (BUFF_STREAM). kmalloc path -- no CMA.
echo "[B] insmod datadev cfgMode=2 (BUFF_STREAM, baseline)"
timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgMode=2
for _ in $(seq 1 15); do
   [ -e "$DEV" ] && break
   sleep 0.5
done
if [ ! -e "$DEV" ]; then
   echo "FAIL: $DEV did not appear after first insmod"
   $SUDO dmesg | tail -50
   exit 1
fi

# Phase C: Remove datadev. Emulator stays loaded -- this is the whole point.
echo "[C] rmmod datadev (emulator keeps running)"
$SUDO rmmod datadev
for _ in $(seq 1 15); do
   [ ! -e "$DEV" ] && break
   sleep 0.5
done

# Phase D: Second insmod with defaulted cfgMode=BUFF_COHERENT (1).
# Pre-fix: GPF in __cma_alloc here. Post-fix: succeeds.
echo "[D] insmod datadev (defaults cfgMode=BUFF_COHERENT -- the crash trigger)"
timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" cfgTxCount=256 cfgRxCount=256 cfgSize=65536
for _ in $(seq 1 15); do
   [ -e "$DEV" ] && break
   sleep 0.5
done
if [ ! -e "$DEV" ]; then
   echo "FAIL: $DEV did not appear after second insmod (expected pre-fix behavior: GPF in __cma_alloc)"
   $SUDO dmesg | tail -50
   exit 1
fi

# Phase E: Cleanup. Errors ignored -- test outcome is already decided above.
$SUDO rmmod datadev 2>/dev/null || true
$SUDO rmmod datadev_emulator 2>/dev/null || true

echo "PASS: second insmod with cfgMode=BUFF_COHERENT succeeded"
exit 0
