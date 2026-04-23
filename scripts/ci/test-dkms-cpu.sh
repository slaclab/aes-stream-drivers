#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Test DKMS CPU installation. Builds CPU tarball, installs via DKMS,
#    and verifies the installation completed successfully.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Installs DKMS package manager, builds CPU tarball, installs and verifies it.
#
# Exit codes: 0=success, 1=DKMS test failed
# ----------------------------------------------------------------------------

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo_step() { echo -e "${GREEN}==>${NC} $1"; }
echo_fail() { echo -e "${RED}FAIL:${NC} $1"; }

echo_step "Testing DKMS CPU installation"

# Install DKMS based on distribution
if [ -f /etc/debian_version ]; then
   # Ubuntu/Debian
   apt-get install -y dkms
elif [ -f /etc/redhat-release ]; then
   # Rocky/RHEL/Fedora - need EPEL (Fedora has dkms in base repos)
   if command -v dnf &> /dev/null; then
      dnf install -y epel-release || true
      dnf install -y dkms
   fi
fi

# Verify DKMS is installed
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

# Test DKMS tarball installation
if [ -f data_dev/driver/datadev-dkms-${GITV}.tar.gz ]; then
   echo "Testing DKMS tarball installation..."
   dkms ldtarball data_dev/driver/datadev-dkms-${GITV}.tar.gz
   dkms install datadev-dkms/${GITV}

   # Verify installation
   dkms status datadev-dkms/${GITV} | grep -q "installed" || {
      echo_fail "DKMS installation did not complete"
      dkms status
      exit 1
   }

   echo "PASS: DKMS installation successful"

   # Cleanup
   dkms remove datadev-dkms/${GITV} --all
else
   echo_fail "DKMS tarball not found: data_dev/driver/datadev-dkms-${GITV}.tar.gz"
   exit 1
fi

echo_step "DKMS CPU test passed"
