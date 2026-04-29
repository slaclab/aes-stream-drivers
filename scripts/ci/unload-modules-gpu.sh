#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Unload GPU modules. Removes datadev, datadev_emulator, and nvidia_p2p_stub
#    in reverse load order. Always succeeds (cleanup step).
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Unloads datadev, datadev_emulator, and nvidia_p2p_stub in reverse load
# order. datadev_emulator holds nvidia_p2p_stub's drain_cb symbol
# references, so the stub must come last. Always succeeds (errors logged
# but not fatal) since this runs in cleanup steps.
#
# Exit codes: 0=success
# ----------------------------------------------------------------------------

# Color output
RED='\033[0;31m'
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

echo_step "Unloading modules (GPU stack)"

$SUDO rmmod datadev 2>/dev/null || echo_warn "datadev not loaded or already unloaded"
sleep 1
$SUDO rmmod datadev_emulator 2>/dev/null || echo_warn "datadev_emulator not loaded or already unloaded"
sleep 1
$SUDO rmmod nvidia_p2p_stub 2>/dev/null || echo_warn "nvidia_p2p_stub not loaded or already unloaded"

echo "Modules unloaded"
