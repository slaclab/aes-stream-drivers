#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Host dependency installer for the aes-ci parity VM. Supports apt
#    (Ubuntu/Debian) and dnf (Rocky/RHEL 9) package managers.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Installs the host package set (qemu-kvm, libvirt, virt-install,
# cloud-image-utils / cloud-utils, virtiofsd, wget, openssh-client) from the
# distro's native package manager. Adds EPEL on Rocky/RHEL 9 because
# cloud-utils lives there.
#
# This script does NOT silently elevate. If run as non-root, it prints the
# exact commands the contributor must run and exits 0. Only root invocations
# actually call the package manager.
#
# Environment variable contract:
#   AES_CI_APT_UPDATE_RETRIES — apt-get update retry count (default: 3)
#
# Exit codes:
#   0 = packages installed OR copy-paste printed (non-root)
#   1 = unsupported distro OR install failed after retries
# ----------------------------------------------------------------------------

set -e

# shellcheck source=lib/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

APT_RETRIES="${AES_CI_APT_UPDATE_RETRIES:-3}"

echo_header "Host Dependency Installer"

# ----------------------------------------------------------------------------
# Distro detection
# ----------------------------------------------------------------------------
if [ -f /etc/os-release ]; then
   . /etc/os-release
   DISTRO_ID="$ID"
   DISTRO_VERSION="$VERSION_ID"
elif [ -f /etc/debian_version ]; then
   DISTRO_ID="debian"
   DISTRO_VERSION=$(cat /etc/debian_version)
elif [ -f /etc/redhat-release ]; then
   if grep -q "Rocky" /etc/redhat-release; then
      DISTRO_ID="rocky"
   else
      DISTRO_ID="rhel"
   fi
   DISTRO_VERSION=$(cat /etc/redhat-release | sed 's/.*release \([0-9]*\).*/\1/')
else
   echo_fail "Cannot detect distribution"
   exit 1
fi

echo_info "Detected distro: $DISTRO_ID $DISTRO_VERSION"

# ----------------------------------------------------------------------------
# Package sets
# ----------------------------------------------------------------------------
APT_PACKAGES=(
   qemu-kvm
   libvirt-daemon-system
   libvirt-clients
   bridge-utils
   virtinst
   cloud-image-utils
   virtiofsd
   openssh-client
   wget
)

DNF_PACKAGES=(
   qemu-kvm
   libvirt
   virt-install
   cloud-utils-growpart
   cloud-utils
   virtiofsd
   bridge-utils
   openssh-clients
   wget
)

# ----------------------------------------------------------------------------
# Non-root fallback — print copy-paste commands, exit 0
# ----------------------------------------------------------------------------
# The script itself never elevates. Exit 0 because "telling the user what to
# do" counts as success — the caller pipeline treats this as an OK result.
if [ "$(id -u)" -ne 0 ]; then
   echo_warn "Not running as root — printing install commands for copy-paste."
   echo ""
   case "$DISTRO_ID" in
      ubuntu|debian)
         echo "Run these commands:"
         echo ""
         echo "   sudo apt-get update && sudo apt-get install -y \\"
         for pkg in "${APT_PACKAGES[@]}"; do
            echo "      $pkg \\"
         done | sed '$ s/ \\$//'
         echo ""
         echo "Then add yourself to the kvm and libvirt groups:"
         echo "   sudo usermod -aG kvm,libvirt $(id -un)"
         echo "   # log out and back in for group change to take effect"
         ;;
      rocky|rhel)
         echo "Run these commands:"
         echo ""
         echo "   sudo dnf install -y epel-release && \\"
         echo "   sudo dnf install -y \\"
         for pkg in "${DNF_PACKAGES[@]}"; do
            echo "      $pkg \\"
         done | sed '$ s/ \\$//'
         echo ""
         echo "Then add yourself to the kvm and libvirt groups:"
         echo "   sudo usermod -aG kvm,libvirt $(id -un)"
         echo "   # log out and back in for group change to take effect"
         ;;
      *)
         echo_fail "Unsupported distro: $DISTRO_ID"
         echo_fail "  See scripts/ci-local/README.md for manual install instructions."
         exit 1
         ;;
   esac
   exit 0
fi

# ----------------------------------------------------------------------------
# Root install path
# ----------------------------------------------------------------------------
case "$DISTRO_ID" in
   ubuntu|debian)
      echo_step "Installing host packages via apt-get"

      # Retry apt-get update up to $APT_RETRIES times
      UPDATE_OK=0
      for i in $(seq 1 "$APT_RETRIES"); do
         if apt-get update; then
            UPDATE_OK=1
            break
         else
            echo_warn "apt-get update failed (attempt $i/$APT_RETRIES), retrying in 10s..."
            sleep 10
         fi
      done

      if [ "$UPDATE_OK" -eq 0 ]; then
         echo_fail "apt-get update failed after $APT_RETRIES attempts"
         exit 1
      fi

      # -y non-interactive; installs all host packages in one apt-get call.
      # DEBIAN_FRONTEND=noninteractive prevents prompts (e.g., iptables-persistent).
      DEBIAN_FRONTEND=noninteractive apt-get install -y "${APT_PACKAGES[@]}" || {
         echo_fail "apt-get install failed"
         exit 1
      }
      ;;

   rocky|rhel)
      echo_step "Installing host packages via dnf (with EPEL)"

      # EPEL must be enabled first — cloud-utils lives there. Verify this
      # still holds on S3DF or other restricted-repo environments before
      # adding/removing packages.
      dnf install -y epel-release || {
         echo_fail "Failed to install epel-release"
         echo_fail "  If EPEL is blocked at your site, install cloud-image-utils manually"
         echo_fail "  from a vendored copy (see scripts/ci-local/README.md troubleshooting)."
         exit 1
      }

      dnf install -y "${DNF_PACKAGES[@]}" || {
         echo_fail "dnf install failed"
         exit 1
      }
      ;;

   *)
      echo_fail "Unsupported distro: $DISTRO_ID"
      echo_fail "  See scripts/ci-local/README.md for manual install instructions."
      exit 1
      ;;
esac

echo_step "Host packages installed successfully"
exit 0
