#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Shared bash helpers for scripts/ci-local/. Provides colored output
#    functions, sudo detection, and NFS-aware cache directory resolution.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Sourced by every scripts/ci-local/*.sh script to provide consistent colored
# output (echo_step / echo_warn / echo_fail / echo_header / echo_info) and
# SUDO detection. Consolidates the block previously duplicated across every
# scripts/ci/*.sh in Phase 1.
#
# Usage (from a sibling script):
#    source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"
#
# Usage (from scripts/run_ci_parity.sh which lives one dir up):
#    source "$(dirname "${BASH_SOURCE[0]}")/ci-local/lib/common.sh"
#
# This file is sourced, not executed — it does NOT set `set -e`. Callers own
# their own error-handling posture.
# ----------------------------------------------------------------------------

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo_step()   { echo -e "${GREEN}==>${NC} $1"; }
echo_warn()   { echo -e "${YELLOW}WARN:${NC} $1"; }
echo_fail()   { echo -e "${RED}FAIL:${NC} $1"; }
echo_info()   { echo -e "${BLUE}INFO:${NC} $1"; }
echo_header() {
   echo -e "${BLUE}========================================${NC}"
   echo -e "${BLUE}$1${NC}"
   echo -e "${BLUE}========================================${NC}"
}

# Detect if we need sudo (0 if already root, "sudo" otherwise)
if [ "$(id -u)" -eq 0 ]; then
   SUDO=""
else
   SUDO="sudo"
fi

# Resolve AES_CI_CACHE_DIR with NFS root_squash awareness.
# libvirtd (root) and libvirt-qemu (uid 64055) both need read access to the
# cache; NFS root_squash maps root->nobody and denies access to anything
# under $HOME. Default to /var/tmp/aes-ci-parity-$USER on NFS homes; caller
# may override via AES_CI_CACHE_DIR. Consumers source common.sh then read
# $AES_CI_CACHE_DIR_RESOLVED rather than recomputing.
# Use `${VAR:-}` so this library is safe to source from callers running with
# `set -u`; a bare `$AES_CI_CACHE_DIR` would abort the caller before the
# resolved value is assigned.
if [ -n "${AES_CI_CACHE_DIR:-}" ]; then
   AES_CI_CACHE_DIR_RESOLVED="$AES_CI_CACHE_DIR"
elif [ "$(stat -f -c %T "$HOME" 2>/dev/null)" = "nfs" ]; then
   AES_CI_CACHE_DIR_RESOLVED="/var/tmp/aes-ci-parity-$(id -un)"
else
   AES_CI_CACHE_DIR_RESOLVED="$HOME/.cache/aes-ci-parity"
fi
