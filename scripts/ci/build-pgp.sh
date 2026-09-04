#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Build the pgpcard driver and its applications, and verify all build
#    artifacts exist.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# The pgpcard driver serves legacy PGP Gen2 (1a4a:2000) and Gen3 (1a4a:2020)
# PCIe cards. There is no PGP personality in emulator/driver, so this phase
# is build coverage plus module load and unload only. It never drives traffic.
#
# Exit codes: 0=success, 1=build failed
# ----------------------------------------------------------------------------

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
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

# Force GITV=emulator for CI test builds, matching build-cpu.sh, so the
# artifact is identifiable as a CI build regardless of branch/tag/dirty state.
GITV=emulator
export GITV
echo_step "Git version: ${GITV}"

echo_step "Building pgpcard driver"
make -C pgpcard/driver clean
make -C pgpcard/driver GITV="$GITV"

echo_step "Building pgpcard applications"
make -C pgpcard/app clean
make -C pgpcard/app

echo_step "Verifying build artifacts"
test -f pgpcard/driver/pgpcard.ko || {
   echo_fail "pgpcard/driver/pgpcard.ko not found"
   exit 1
}

for app in pgpGetStatus pgpLoopTest pgpPromLoad pgpPromVerify pgpRead \
           pgpSetData pgpSetDebug pgpSetLoop pgpWrite; do
   test -f "pgpcard/app/bin/${app}" || {
      echo_fail "pgpcard/app/bin/${app} not found"
      exit 1
   }
done

# Both device IDs must be advertised, or the driver will never bind. modinfo
# is not available in every minimal container, so fall back to a strings scan
# of the module for the PCI alias.
echo_step "Verifying PCI device aliases"
if command -v modinfo >/dev/null 2>&1; then
   ALIASES="$(modinfo pgpcard/driver/pgpcard.ko 2>/dev/null | grep '^alias:' || true)"
else
   ALIASES="$(strings pgpcard/driver/pgpcard.ko 2>/dev/null | grep '^pci:v00001A4A' || true)"
fi

for did in 00002000 00002020; do
   echo "$ALIASES" | grep -qi "v00001A4Ad${did}" || {
      echo_fail "pgpcard.ko does not advertise PCI device 1a4a:${did#0000}"
      echo "aliases found: ${ALIASES:-<none>}"
      exit 1
   }
done

echo_step "All build artifacts present"
