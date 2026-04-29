#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Idempotent retrofit of virtiofs onto an existing aes-ci parity VM.
#    Adds the virtiofs filesystem element and memorybacking via virt-xml.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Migration tool for contributors who already have a VM provisioned
# and don't want to pay the 15-20 min re-provision cost just to pick up
# the virtiofs mount. Hot-adds a <filesystem type='mount'
# driver='virtiofs'> element via virt-xml --add-device + <memoryBacking
# source='memfd' access='shared'/> via virt-xml --edit, then restarts the
# domain so libvirtd spawns virtiofsd.
#
# Idempotent: if the domain already has a virtiofs filesystem element, exits
# 0 with "nothing to do". Safe to re-run.
#
# NEW contributors do NOT need this — provision_vm.sh Stage 7 bakes virtiofs
# into virt-install at first boot.
#
# The guest must ALSO have /mnt/aes-stream-drivers in /etc/fstab (added by
# cloud-init write_files in user-data.yaml). A contributor upgrading a
# VM without re-provisioning must edit /etc/fstab inside the guest
# manually OR run ./scripts/run_ci_parity.sh --reset which re-provisions
# with the new user-data (~15 min). This script only wires the host side.
#
# Environment variable contract:
#   AES_CI_DOMAIN        (default: aes-ci)
#   AES_CI_REPO_SOURCE   (default: git rev-parse --show-toplevel or $PWD)
#
# Exit codes:
#   0 = virtiofs now present on the domain (enabled this run, or already was)
#   1 = virt-xml / virsh failure
#   3 = domain missing (run ./scripts/run_ci_parity.sh first)
#   5 = unsupported flag / usage error
# ----------------------------------------------------------------------------

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"

DOM="${AES_CI_DOMAIN:-aes-ci}"
REPO_HOST_PATH="${AES_CI_REPO_SOURCE:-$(git -C "$SCRIPT_DIR/../.." rev-parse --show-toplevel 2>/dev/null || pwd)}"

case "${1:-}" in
   -h|--help)
      cat <<EOF
Usage: $0

Retrofit virtiofs onto an existing aes-ci parity VM. Idempotent.
New contributors don't need this — provision_vm.sh bakes virtiofs
in at first boot.

Environment:
   AES_CI_DOMAIN        libvirt domain name (default: aes-ci)
   AES_CI_REPO_SOURCE   host path to share (default: repo root)
EOF
      exit 0
      ;;
   "")
      ;;
   *)
      echo_fail "Unknown flag: $1"
      exit 5
      ;;
esac

echo_header "enable_virtiofs.sh — retrofit virtiofs on $DOM"

if ! virsh --connect qemu:///system dominfo "$DOM" >/dev/null 2>&1; then
   echo_fail "Domain $DOM not found."
   echo_fail "  Run ./scripts/run_ci_parity.sh first to provision the VM."
   exit 3
fi

if virsh --connect qemu:///system dumpxml "$DOM" | grep -q "driver type='virtiofs'"; then
   echo_info "Virtiofs already configured on $DOM; nothing to do."
   exit 0
fi

echo_step "Adding virtiofs filesystem to $DOM (source: $REPO_HOST_PATH)"
echo_info "Persistent XML changes require domain shutdown — restarting $DOM"

# Persistent XML edits require the domain be shut down.
virsh --connect qemu:///system shutdown "$DOM" 2>/dev/null || true
for _ in $(seq 1 60); do
   STATE=$(virsh --connect qemu:///system dominfo "$DOM" 2>/dev/null | awk '/^State:/ {print $2}')
   if [ "$STATE" = "shut" ]; then
      break
   fi
   sleep 1
done
if [ "$STATE" != "shut" ]; then
   echo_warn "Domain did not shut down cleanly within 60s; forcing destroy"
   virsh --connect qemu:///system destroy "$DOM" 2>/dev/null || true
fi

virt-xml --connect qemu:///system "$DOM" --add-device \
   --filesystem "source.dir=$REPO_HOST_PATH,target.dir=aes-stream-drivers,type=mount,accessmode=passthrough,driver.type=virtiofs" \
   || {
      echo_fail "virt-xml --add-device failed"
      exit 1
   }

virt-xml --connect qemu:///system "$DOM" --edit \
   --memorybacking "source.type=memfd,access.mode=shared" \
   || {
      echo_fail "virt-xml --edit memorybacking failed"
      exit 1
   }

echo_step "Starting $DOM"
virsh --connect qemu:///system start "$DOM" \
   || {
      echo_fail "virsh start $DOM failed"
      exit 1
   }

echo_step "Virtiofs retrofit complete on $DOM."
echo_info "The guest should pick up /mnt/aes-stream-drivers via /etc/fstab at next boot."
echo_info "If the guest doesn't have the fstab entry, either:"
echo_info "  (a) ssh in and add it manually:"
echo_info "      echo 'aes-stream-drivers /mnt/aes-stream-drivers virtiofs rw,nofail 0 0' | sudo tee -a /etc/fstab"
echo_info "  (b) run ./scripts/run_ci_parity.sh --reset to fully re-provision with current user-data."
exit 0
