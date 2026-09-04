#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Unload the pgpcard module. Always succeeds (cleanup step).
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Unloads pgpcard. Errors are logged but not fatal, since this runs in cleanup
# steps. With no PGP card present, PgpCard_Remove never runs, so this exercises
# module_exit and pci_unregister_driver only.
#
# Exit codes: 0=success
# ----------------------------------------------------------------------------

# Color output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo_step() { echo -e "${GREEN}==>${NC} $1"; }
echo_warn() { echo -e "${YELLOW}WARN:${NC} $1"; }

# Detect if we need sudo
if [ "$(id -u)" -eq 0 ]; then
   SUDO=""
else
   SUDO="sudo"
fi

echo_step "Unloading pgpcard"

$SUDO rmmod pgpcard 2>/dev/null || echo_warn "pgpcard not loaded or already unloaded"

echo "Modules unloaded"
