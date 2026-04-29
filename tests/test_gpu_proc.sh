#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    GPU /proc interface verification. Asserts /proc/datadev_0 shows the
#    GPU-enabled driver state with GpuAsyncCore version 4.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Asserts /proc/datadev_0 shows the GPU-enabled driver state when running
# against the emulator + nvidia_p2p_stub + DATA_GPU build.
#
# Expected strings (flexible whitespace grep):
#   - "GPUAsync Support : Enabled"    (from dma_common.c Dma_ShowInfo)
#   - "GpuAsyncCore Version : 4"      (from gpu_async.c Gpu_Show)
#   - "Max Buffers : 4"               (from gpu_async.c Gpu_Show)
#
# Environment:
#   PROC   Path to /proc entry (default: /proc/datadev_0)
#
# Exit code: 0 if all assertions pass, 1 otherwise.
# ----------------------------------------------------------------------------

set -uo pipefail

PROC="${PROC:-/proc/datadev_0}"

if [ ! -r "$PROC" ]; then
   echo "FAIL: $PROC not readable (datadev not loaded?)"
   exit 1
fi

ERRS=0

check_line() {
   local label="$1"
   local regex="$2"
   if grep -Eq "$regex" "$PROC"; then
      echo "[PASS] $label"
   else
      echo "[FAIL] $label"
      echo "       regex: $regex"
      echo "       PROC sample:"
      grep -E '^[[:space:]]*(GPU|Gpu|Max)' "$PROC" || echo "       (no matching lines)"
      ERRS=$((ERRS + 1))
   fi
}

check_line "GPUAsync Support Enabled"  '^[[:space:]]*GPUAsync[[:space:]]*Support[[:space:]]*:[[:space:]]*Enabled'
check_line "GpuAsyncCore Version 4"     '^[[:space:]]*GpuAsyncCore[[:space:]]*Version[[:space:]]*:[[:space:]]*4'
check_line "Max Buffers 4"              '^[[:space:]]*Max[[:space:]]*Buffers[[:space:]]*:[[:space:]]*4'

if [ "$ERRS" -eq 0 ]; then
   echo "=== test_gpu_proc: PASS ==="
   exit 0
else
   echo "=== test_gpu_proc: FAIL ($ERRS error(s)) ==="
   exit 1
fi
