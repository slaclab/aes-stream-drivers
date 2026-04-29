#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    KVM preflight check. Verifies /dev/kvm presence, permissions, nested
#    virtualization, and required host tools for the aes-ci parity VM.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Verifies the three conditions required for the aes-ci parity VM to boot
# with KVM acceleration, plus the required set of host tools.
#
# Failure modes produce distinct exit codes and distinct operator messages
# so the contributor knows which fix to apply without diving into docs.
#
# Exit codes:
#   0 = all checks passed (KVM usable; nested-virt warning does not fail)
#   1 = /dev/kvm missing (firmware or outer hypervisor)
#   2 = /dev/kvm present but not accessible to user (group membership)
#   4 = required host tool missing (run install-host-deps.sh)
# ----------------------------------------------------------------------------

set -e

# shellcheck source=lib/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

echo_header "KVM Preflight"

# ----------------------------------------------------------------------------
# Check 1: /dev/kvm device node presence
# ----------------------------------------------------------------------------
echo_step "Checking /dev/kvm device node"
if [ ! -c /dev/kvm ]; then
   echo_fail "/dev/kvm is missing — KVM is not enabled on this host."
   echo_fail "  - Verify CPU virtualization in firmware (VT-x / AMD-V)."
   echo_fail "  - If this host is itself a VM, enable nested virtualization on the outer hypervisor."
   exit 1
fi

# ----------------------------------------------------------------------------
# Check 2: /dev/kvm permission / group membership
# ----------------------------------------------------------------------------
echo_step "Checking /dev/kvm permissions for user $(id -un)"
if [ ! -r /dev/kvm ] || [ ! -w /dev/kvm ]; then
   echo_fail "/dev/kvm exists but is not accessible to user $(id -un)."
   echo_fail "  Add yourself to the kvm and libvirt groups, then log out and back in:"
   echo_fail "      sudo usermod -aG kvm,libvirt $(id -un)"
   exit 2
fi

# Warn (not fail) if user is not in libvirt group — virtiofs needs the
# system URI which requires libvirt group membership.
if ! id -nG | tr ' ' '\n' | grep -qx libvirt; then
   echo_warn "User $(id -un) is not in the libvirt group."
   echo_warn "  Basic KVM will work, but virtiofs requires libvirt group membership."
   echo_warn "  Fix now: sudo usermod -aG libvirt $(id -un) && log out/in"
fi

# ----------------------------------------------------------------------------
# Check 3: nested virtualization flag (Intel / AMD)
# ----------------------------------------------------------------------------
echo_step "Checking nested virtualization flag"
NESTED="?"
if [ -r /sys/module/kvm_intel/parameters/nested ]; then
   NESTED=$(cat /sys/module/kvm_intel/parameters/nested)
elif [ -r /sys/module/kvm_amd/parameters/nested ]; then
   NESTED=$(cat /sys/module/kvm_amd/parameters/nested)
fi

case "$NESTED" in
   Y|1)
      echo_step "Nested virtualization enabled (nested=$NESTED)"
      ;;
   N|0)
      echo_warn "Nested virtualization is disabled (nested=$NESTED)."
      echo_warn "  If this host is itself a VM, the guest will fall back to TCG and be"
      echo_warn "  too slow to use as a CI gate."
      echo_warn "  Outer hypervisor fix depends on platform (VMware: vhv.enable=TRUE,"
      echo_warn "  VirtualBox: modifyvm --nested-hw-virt on, KVM: modprobe kvm_intel nested=1)."
      # Non-fatal — document the state, don't fail.
      ;;
   *)
      echo_warn "Could not determine nested-virt state (non-Intel/AMD host, or kvm module not loaded?)"
      ;;
esac

# ----------------------------------------------------------------------------
# Check 4: required host tools present in PATH
# ----------------------------------------------------------------------------
echo_step "Checking host tool availability"
MISSING_TOOLS=()

# Tools with no fallback — required
for cmd in qemu-system-x86_64 qemu-img virt-install virsh ssh ssh-keygen sha256sum; do
   if ! command -v "$cmd" >/dev/null 2>&1; then
      MISSING_TOOLS+=("$cmd")
   fi
done

# Fallback group: wget OR curl
if ! command -v wget >/dev/null 2>&1 && ! command -v curl >/dev/null 2>&1; then
   MISSING_TOOLS+=("wget-or-curl")
fi

# Fallback group: cloud-localds OR genisoimage OR mkisofs
if ! command -v cloud-localds >/dev/null 2>&1 \
      && ! command -v genisoimage >/dev/null 2>&1 \
      && ! command -v mkisofs >/dev/null 2>&1; then
   MISSING_TOOLS+=("cloud-localds-or-genisoimage")
fi

if [ ${#MISSING_TOOLS[@]} -gt 0 ]; then
   echo_fail "Missing required host tools: ${MISSING_TOOLS[*]}"
   echo_fail "  Run: bash scripts/ci-local/install-host-deps.sh"
   echo_fail "  Or install packages listed in scripts/ci-local/README.md"
   exit 4
fi

# ----------------------------------------------------------------------------
# All checks passed
# ----------------------------------------------------------------------------
echo_step "KVM preflight passed (nested=$NESTED)"
exit 0
