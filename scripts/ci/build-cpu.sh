#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Build CPU modules and test applications. Builds emulator, datadev
#    driver (CPU only), and verifies all build artifacts exist.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Builds emulator, datadev driver (CPU only), and test applications.
# Verifies all build artifacts exist.
#
# Exit codes: 0=success, 1=build failed
# ----------------------------------------------------------------------------

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo_step() { echo -e "${GREEN}==>${NC} $1"; }
echo_fail() { echo -e "${RED}FAIL:${NC} $1"; }

# Determine kernel version to build against. install-deps.sh writes this to
# $GITHUB_ENV and /tmp/ci_kver; fall back to uname -r for local runs.
if [ -z "${CI_KVER:-}" ] && [ -f /tmp/ci_kver ]; then
   CI_KVER="$(cat /tmp/ci_kver)"
fi
if [ -z "${CI_KVER:-}" ]; then
   CI_KVER="$(uname -r)"
fi
export KVER="$CI_KVER"

echo_step "Building against kernel ${KVER}"

# Compute git version once here where we're in the repo root.
# The Makefile's own git logic fails under kbuild (runs from kernel build
# dir, outside the repo).  Passing GITV= on the command line skips the
# Makefile's ifndef GITV block entirely.
GITV=$(git describe --tags 2>/dev/null || git rev-parse --short HEAD 2>/dev/null || echo "emulator")
GITD=$(git status --short -uno | wc -l)
if [ "$GITD" -ne 0 ]; then GITV="${GITV}-dirty"; fi
export GITV
echo_step "Git version: ${GITV}"

echo_step "Building nvidia_p2p_stub module"
make -C emulator/gpu_stub clean
make -C emulator/gpu_stub

# Build emulator after stub so KBUILD_EXTRA_SYMBOLS in emulator/driver/Makefile
# can resolve emu_gpu_register_drain_cb and emu_gpu_unregister_drain_cb at
# modpost time.
echo_step "Building emulator module"
make -C emulator/driver clean
make -C emulator/driver

echo_step "Building datadev driver (CPU)"
make -C data_dev/driver clean
make -C data_dev/driver GITV="$GITV"

echo_step "Building test applications"
make -C data_dev/app clean
make -C data_dev/app

echo_step "Verifying build artifacts"
test -f emulator/gpu_stub/nvidia_p2p_stub.ko || {
   echo_fail "emulator/gpu_stub/nvidia_p2p_stub.ko not found"
   exit 1
}

test -f emulator/gpu_stub/Module.symvers || {
   echo_fail "emulator/gpu_stub/Module.symvers not found"
   exit 1
}

test -f emulator/driver/datadev_emulator.ko || {
   echo_fail "emulator/driver/datadev_emulator.ko not found"
   exit 1
}

test -f data_dev/driver/datadev.ko || {
   echo_fail "data_dev/driver/datadev.ko not found"
   exit 1
}

test -f data_dev/app/bin/dmaLoopTest || {
   echo_fail "data_dev/app/bin/dmaLoopTest not found"
   exit 1
}

test -f data_dev/app/bin/dmaIoctlTest || {
   echo_fail "data_dev/app/bin/dmaIoctlTest not found"
   exit 1
}

test -f data_dev/app/bin/dmaFileOpsTest || {
   echo_fail "data_dev/app/bin/dmaFileOpsTest not found"
   exit 1
}

test -f data_dev/app/bin/dmaErrorTest || {
   echo_fail "data_dev/app/bin/dmaErrorTest not found"
   exit 1
}

echo_step "All build artifacts present"
