#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Load the pgpcard module with a timeout-wrapped insmod and initstate
#    polling for readiness confirmation.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# What this does and does NOT cover:
#
# pgpcard is a PCI driver for legacy PGP Gen2 (1a4a:2000) and Gen3
# (1a4a:2020) cards, and emulator/driver has no PGP personality: it emulates
# only 1a4a:2030 with an AXIS Gen2 register map. No such card exists in CI, so
# PgpCard_Probe never runs. This validates module_init, symbol resolution
# against the running kernel, and module_exit. It does NOT exercise probe,
# remove, DMA, or interrupts. In particular the hwFunc->irqEnable path in
# Dma_Clean is only reachable with a real card present.
#
# A load with no matching device is expected to succeed: PgpCard_Init only
# memsets gDmaDevices and calls pci_register_driver, which returns 0 when
# nothing matches.
#
# Exit codes: 0=success, 1=timeout/failure/refused
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

TIMEOUT_SEC="${TIMEOUT_SEC:-15}"
INSMOD_TIMEOUT_SEC="${INSMOD_TIMEOUT_SEC:-120}"

# Refuse to insmod when the container's kernel-headers install does not match
# the running host kernel. [ -e ] follows symlinks and returns false for a
# broken symlink, catching both "headers missing" and "headers for the wrong
# kernel revision".
if [ ! -e "/lib/modules/$(uname -r)/build" ]; then
   echo_fail "Kernel headers for $(uname -r) not installed in this container, refusing to insmod"
   exit 1
fi

# The built module must match the running kernel, otherwise insmod fails with
# "Invalid module format" and the real reason is buried in dmesg. Fail early
# with a clear message instead. This is the expected state on any cell where
# install-deps.sh reported CI_HOST_MATCH=0.
if command -v modinfo >/dev/null 2>&1; then
   MOD_VERMAGIC="$(modinfo -F vermagic pgpcard/driver/pgpcard.ko 2>/dev/null | awk '{print $1}')"
   if [ -n "$MOD_VERMAGIC" ] && [ "$MOD_VERMAGIC" != "$(uname -r)" ]; then
      echo_fail "pgpcard.ko was built for ${MOD_VERMAGIC} but the running kernel is $(uname -r)"
      echo "This cell should be build-only (load_test 0). See CI_HOST_MATCH in scripts/ci/install-deps.sh."
      exit 1
   fi
fi

# Record that load-modules was reached BEFORE the marker injection attempt.
# This survives unload-modules and is the signal check-dmesg.sh uses to
# distinguish "build-only cell, dmesg gate truly N/A" from "load was attempted
# but /dev/kmsg dropped the marker silently".
echo 1 > /tmp/ci_load_attempted

# Inject a unique baseline marker so check-dmesg.sh can extract a
# "since this moment" delta.
CI_DMESG_MARKER="BASELINE-aes-ci-$(cat /proc/sys/kernel/random/uuid)"
echo "$CI_DMESG_MARKER" | $SUDO tee /dev/kmsg > /dev/null
if $SUDO dmesg | grep -qF "$CI_DMESG_MARKER"; then
   echo "$CI_DMESG_MARKER" > /tmp/ci_dmesg_marker
else
   echo_warn "dmesg baseline marker did not land; check-dmesg.sh will scan the full ring"
fi

echo_step "Loading pgpcard, insmod timeout ${INSMOD_TIMEOUT_SEC}s, initstate timeout ${TIMEOUT_SEC}s"

timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod pgpcard/driver/pgpcard.ko || {
   rc=$?
   echo_fail "insmod pgpcard failed or timed out (exit $rc)"
   echo "initstate: $(cat /sys/module/pgpcard/initstate 2>/dev/null || echo 'missing')"
   $SUDO dmesg | tail -50
   exit 1
}

timeout $TIMEOUT_SEC bash -c "until [ \"\$(cat /sys/module/pgpcard/initstate 2>/dev/null)\" = live ]; do sleep 0.5; done" || {
   echo_fail "pgpcard initstate did not reach 'live' within ${TIMEOUT_SEC}s"
   echo "initstate: $(cat /sys/module/pgpcard/initstate 2>/dev/null || echo 'missing')"
   $SUDO dmesg | tail -50
   exit 1
}
echo "pgpcard is live"

# Report whether a real card bound, purely informational. CI has none, so the
# expected output here is "no PGP card present".
if [ -d /sys/bus/pci/drivers/pgpcard ]; then
   BOUND="$(find /sys/bus/pci/drivers/pgpcard -maxdepth 1 -name '0000:*' -printf '%f ' 2>/dev/null || true)"
   if [ -n "$BOUND" ]; then
      echo_step "PGP card(s) bound: ${BOUND}"
   else
      echo_step "No PGP card present; probe and remove paths are not exercised"
   fi
fi

echo_step "pgpcard module load complete"
