#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    DKMS CPU smoke test. Builds CPU tarball, loads it into the DKMS tree
#    via ldtarball, and verifies the package is registered (added). Does NOT
#    build or install the module — that requires matched kernel headers which
#    are only available on the load_test cell.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo_step() { echo -e "${GREEN}==>${NC} $1"; }
echo_fail() { echo -e "${RED}FAIL:${NC} $1"; }

echo_step "DKMS CPU smoke test (tarball + add only)"

# Install DKMS based on distribution
if [ -f /etc/debian_version ]; then
   apt-get install -y dkms
elif [ -f /etc/redhat-release ]; then
   if command -v dnf &> /dev/null; then
      dnf install -y epel-release || true
      dnf install -y dkms
   elif command -v yum &> /dev/null; then
      yum install -y epel-release || true
      yum install -y dkms
   fi
fi

dkms --version || {
   echo_fail "DKMS installation failed"
   exit 1
}

# Compute git version the same way build-cpu.sh does
GITV=$(git describe --tags 2>/dev/null || git rev-parse --short HEAD 2>/dev/null || echo "emulator")
GITD=$(git status --short -uno 2>/dev/null | wc -l)
if [ "$GITD" -ne 0 ]; then GITV="${GITV}-dirty"; fi
export GITV

# Build DKMS tarball
make -C data_dev/driver dkms GITV="$GITV"

if [ -f data_dev/driver/datadev-dkms-${GITV}.tar.gz ]; then
   echo "Loading DKMS tarball..."
   dkms ldtarball data_dev/driver/datadev-dkms-${GITV}.tar.gz

   # Verify the package was registered
   dkms status datadev-dkms/${GITV} | grep -q "added" || {
      echo_fail "DKMS package not registered after ldtarball"
      dkms status
      exit 1
   }

   echo "PASS: DKMS CPU tarball registered successfully"

   # Cleanup
   dkms remove datadev-dkms/${GITV} --all
else
   echo_fail "DKMS tarball not found: data_dev/driver/datadev-dkms-${GITV}.tar.gz"
   exit 1
fi

echo_step "DKMS CPU smoke test passed"
