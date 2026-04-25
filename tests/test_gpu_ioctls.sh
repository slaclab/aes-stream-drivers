#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    GPU ioctl test wrapper. Invokes dmaGpuIoctlTest against the current
#    datadev character device.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Invokes dmaGpuIoctlTest against the current datadev character device.
# Returns the exit code of dmaGpuIoctlTest (0 = all GPU ioctls pass,
# 1 = at least one failure).
#
# Environment variable contract:
#   DEV       Path to character device node (default: /dev/datadev_0)
#   APP_BIN   Directory containing test binaries
#             (default: data_dev/app/bin relative to repo root)
# ----------------------------------------------------------------------------

set -uo pipefail

DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"

if [ ! -x "$APP_BIN/dmaGpuIoctlTest" ]; then
   echo "ERROR: $APP_BIN/dmaGpuIoctlTest not found or not executable"
   echo "       Build with: make -C data_dev/app"
   exit 2
fi

# Outer `timeout` guards against a CI hang if dmaGpuIoctlTest wedges
# inside an ioctl (e.g. a wedged driver path). Matches the
# timeout-wrapping convention used throughout the rest of the suite.
# 30 s is ample for the 6 GPU async ioctls this binary exercises; rc=124
# surfaces as a non-zero exit and is distinguished below.
timeout 30 "$APP_BIN/dmaGpuIoctlTest" -p "$DEV"
RC=$?
if [ "$RC" -eq 124 ]; then
   echo "FAIL: dmaGpuIoctlTest timed out (exit 124)"
   exit 1
fi
exit "$RC"
