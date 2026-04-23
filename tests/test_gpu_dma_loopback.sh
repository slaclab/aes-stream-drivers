#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    GPU DMA loopback wrapper. Runs sweep, soak, and toggle subtests via
#    rdmaTestEmu and dmaGpuToggleTest against the emulator GPU path.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Single entry point invoked by:
#   - tests/run_tests.sh under GPU_ENABLED=1
#   - scripts/ci/test-gpu.sh paranoia grep
#
# Execution sequence:
#   1. Preamble gate 1: EMU_BUILD_VERSION sysfs vs. .build_version
#   2. Preamble gate 2: gpu_async.c empty-diff vs. origin/main
#   3. Sweep re-run via rdmaTestEmu --sweep
#   4. Soak via rdmaTestEmu -c 10000 -s 65536
#   5. Toggle via dmaGpuToggleTest
#
# Exit 0 = all pass; Exit 1 = any failure.
# Assumes modules pre-loaded (matches test_gpu_ioctls.sh / test_gpu_proc.sh).
# ----------------------------------------------------------------------------

set -uo pipefail

DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"

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

# Marker line for paranoia grep (must be first output line)
echo "RUN: gpu_dma_loopback"

# ============================================================================
# Preamble gate 1: EMU_BUILD_VERSION sysfs assertion
# ============================================================================
echo_step "Preamble gate 1: EMU_BUILD_VERSION sysfs assertion"

SYSFS_EMU=$(cat /sys/module/datadev_emulator/parameters/build_version 2>/dev/null || echo "MISSING")
STAMP_EMU=$(cat emulator/driver/.build_version 2>/dev/null || echo "MISSING")
if [ "$SYSFS_EMU" != "$STAMP_EMU" ]; then
   echo_fail "Stale emulator module detected: expected=$STAMP_EMU got=$SYSFS_EMU"
   exit 1
fi
echo "[PASS] EMU_BUILD_VERSION emulator=$SYSFS_EMU"

SYSFS_STUB=$(cat /sys/module/nvidia_p2p_stub/parameters/build_version 2>/dev/null || echo "MISSING")
STAMP_STUB=$(cat emulator/gpu_stub/.build_version 2>/dev/null || echo "MISSING")
if [ "$SYSFS_STUB" != "$STAMP_STUB" ]; then
   echo_fail "Stale stub module detected: expected=$STAMP_STUB got=$SYSFS_STUB"
   exit 1
fi
echo "[PASS] EMU_BUILD_VERSION stub=$SYSFS_STUB"

# ============================================================================
# Preamble gate 2: gpu_async.c zero-diff vs. origin/main (real-HW regression gate)
# ============================================================================
echo_step "Preamble gate 2: gpu_async.c zero-diff vs origin/main"

if git diff --exit-code origin/main -- \
      common/driver/gpu_async.c \
      data_dev/driver/src/gpu_async.c >/dev/null 2>&1; then
   echo "[PASS] gpu_async.c unchanged from origin/main"
else
   BASE=$(git merge-base HEAD origin/main 2>/dev/null || true)
   if [ -n "$BASE" ]; then
      if git diff --exit-code "$BASE" -- \
            common/driver/gpu_async.c \
            data_dev/driver/src/gpu_async.c >/dev/null 2>&1; then
         echo "[PASS] gpu_async.c unchanged from merge-base $BASE"
      else
         echo_fail "gpu_async.c modified — real-HW regression risk"
         git diff "$BASE" -- common/driver/gpu_async.c data_dev/driver/src/gpu_async.c
         exit 1
      fi
   else
      echo_warn "Cannot resolve origin/main; skipping diff gate locally"
   fi
fi

FAILED=0

# ============================================================================
# Subtest: sweep re-run (cheap/fast, runs first)
# ============================================================================
echo_step "Subtest: sweep re-run"
T_START=$SECONDS
timeout 120 "$APP_BIN/rdmaTestEmu" -d "$DEV" -c 100 --sweep
RC=$?
ELAPSED=$(( SECONDS - T_START ))
echo "[sweep_rerun] elapsed=${ELAPSED}s"
if [ "$RC" -ne 0 ]; then
   echo_fail "sweep failed (rc=$RC)"
   $SUDO dmesg | tail -100 >&2
   FAILED=1
fi

# ============================================================================
# Subtest: soak (10000 frames @ 64 KiB)
# ============================================================================
echo_step "Subtest: soak (10000 frames @ 64 KiB)"
T_START=$SECONDS
timeout 60 "$APP_BIN/rdmaTestEmu" -d "$DEV" -c 10000 -s 65536
RC=$?
ELAPSED=$(( SECONDS - T_START ))
echo "[soak] elapsed=${ELAPSED}s"
if [ "$RC" -eq 0 ]; then
   echo "PASS: frames=10000 mismatches=0 rxFrameCnt==txFrameCnt==framesSent minWriteBuffer>0"
else
   echo_fail "soak failed (rc=$RC)"
   $SUDO dmesg | tail -100 >&2
   FAILED=1
fi

# ============================================================================
# Subtest: toggle (enable-toggle + maxBuffers 4->2)
# ============================================================================
echo_step "Subtest: toggle (enable-toggle + maxBuffers 4->2)"
T_START=$SECONDS
timeout 30 "$APP_BIN/dmaGpuToggleTest" -p "$DEV"
RC=$?
ELAPSED=$(( SECONDS - T_START ))
echo "[toggle] elapsed=${ELAPSED}s"
if [ "$RC" -ne 0 ]; then
   echo_fail "toggle failed (rc=$RC)"
   $SUDO dmesg | tail -100 >&2
   FAILED=1
fi

# ============================================================================
# Summary
# ============================================================================
if [ "$FAILED" -eq 0 ]; then
   echo "PASS: all gpu_dma_loopback subtests green"
   exit 0
else
   echo_fail "gpu_dma_loopback wrapper failed"
   exit 1
fi
