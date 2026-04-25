#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Install build dependencies for multi-distro CI. Auto-detects distribution
#    and installs kernel headers, build tools, and dependencies with retry logic.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Auto-detects distribution and installs kernel headers, build tools, and
# dependencies with retry logic for package manager updates.
#
# Tries to install headers matching the running host kernel (uname -r). If
# those packages are not available in the distro's repos (common when the
# host runs an Azure/HWE kernel not shipped by older/other distros), falls
# back to the distro's default/generic kernel headers so the module can at
# least be compiled for cross-distro coverage.
#
# Exports the actually-installed kernel version and host-match flag via
# $GITHUB_ENV (for subsequent workflow steps) and /tmp/ci_kver,
# /tmp/ci_host_match (for local script reuse).
#
# Supports: Ubuntu/Debian (apt-get), Rocky Linux 9 / Fedora (dnf)
#
# Exit codes: 0=success, 1=failure after retries
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

# Export a CI variable to GITHUB_ENV (if available) and /tmp for downstream use
export_ci_var() {
   local name="$1"
   local value="$2"
   export "$name"="$value"
   if [ -n "$GITHUB_ENV" ] && [ -w "$GITHUB_ENV" ]; then
      echo "${name}=${value}" >> "$GITHUB_ENV"
   fi
   case "$name" in
      CI_KVER)       echo "$value" > /tmp/ci_kver ;;
      CI_HOST_MATCH) echo "$value" > /tmp/ci_host_match ;;
   esac
}

# Clear per-cell side-channel files at the very start of every cell. On
# ephemeral GHA runners /tmp is fresh, but self-hosted runners and local
# manual reruns can leave /tmp/ci_load_attempted or /tmp/ci_dmesg_marker
# from a prior cell. Stale /tmp/ci_load_attempted on a build-only cell
# would push check-dmesg.sh into the kmsg-drop fallback path and run a
# full-ring scan against unrelated history. Both files are recreated by
# load-modules-*.sh on load_test cells; build-only cells leave them absent.
rm -f /tmp/ci_load_attempted /tmp/ci_dmesg_marker

echo_step "Installing build dependencies"

# Detect distribution
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

HOST_KVER="$(uname -r)"

echo "Detected: $DISTRO_ID $DISTRO_VERSION"
echo "Host kernel: $HOST_KVER"

# Detect the kernel version for which headers were actually installed. The
# output is the newest entry in the standard locations; empty if nothing was
# installed. We use this to tell downstream steps which KVER to build against.
# Optional arg $1: a KVER to exclude (used to skip bind-mounted host headers).
detect_installed_kver() {
   local exclude="${1:-}"
   local found=""
   if [ -d /lib/modules ]; then
      for d in $(ls -1 /lib/modules 2>/dev/null | sort -V); do
         [ "$d" = "$exclude" ] && continue
         if [ -d "/lib/modules/$d/build" ] || [ -e "/lib/modules/$d/build" ]; then
            found="$d"
         fi
      done
   fi
   if [ -z "$found" ] && [ -d /usr/src/kernels ]; then
      for d in $(ls -1 /usr/src/kernels 2>/dev/null | sort -V); do
         [ "$d" = "$exclude" ] && continue
         found="$d"
      done
   fi
   echo "$found"
}

# When building against bind-mounted host kernel headers, the kernel's
# kbuild system requires the same GCC major version that built the kernel.
# Extract the version from /proc/version and install the matching compiler
# if the container doesn't already have it.
#
# Returns 0 if a compatible gcc-N is available after this function,
# non-zero if the distro cannot provide a suitable compiler (caller
# should fall back to distro-native kernel headers).
ensure_kernel_gcc() {
   local kgcc=""
   # awk extraction (not grep -oP): PCRE is GNU-grep-specific and the prior
   # Copilot review flagged the equivalent pattern in tests/test_data_integrity.sh
   # as a portability hazard on musl/BusyBox minimal images.
   kgcc=$(awk 'match($0, /gcc-[0-9]+/) { print substr($0, RSTART+4, RLENGTH-4); exit }' /proc/version 2>/dev/null)
   if [ -z "$kgcc" ]; then
      echo_warn "Cannot determine kernel build GCC version from /proc/version"
      return 1
   fi
   if command -v "gcc-${kgcc}" &>/dev/null; then
      echo_step "Kernel build GCC version gcc-${kgcc} already available"
      return 0
   fi
   echo_step "Installing gcc-${kgcc} to match kernel build compiler"
   if command -v apt-get &>/dev/null; then
      if ! apt-get install -y "gcc-${kgcc}"; then
         echo_warn "gcc-${kgcc} not available; trying highest available gcc"
         local best=""
         for v in $(apt-cache search '^gcc-[0-9]+$' 2>/dev/null \
                    | awk 'match($0, /gcc-[0-9]+/) { print substr($0, RSTART+4, RLENGTH-4) }' \
                    | sort -n); do
            best="$v"
         done
         if [ -n "$best" ]; then
            apt-get install -y "gcc-${best}" || true
            if command -v "gcc-${best}" &>/dev/null; then
               # DKMS 2.8 hardcodes PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/lib/dkms
               # at startup, so /usr/local/bin is invisible to its make sub-shell.
               # Place the shim in /usr/bin so `dkms install` can resolve it.
               ln -sf "$(command -v "gcc-${best}")" "/usr/bin/gcc-${kgcc}"
               echo_step "Symlinked gcc-${kgcc} -> gcc-${best}"
            fi
         fi
      fi
   elif command -v dnf &>/dev/null; then
      if ! dnf install -y "gcc-toolset-${kgcc}-gcc" 2>/dev/null; then
         dnf install -y "gcc" 2>/dev/null || true
      fi
      if ! command -v "gcc-${kgcc}" &>/dev/null; then
         local tsgcc="/opt/rh/gcc-toolset-${kgcc}/root/usr/bin/gcc"
         if [ -x "$tsgcc" ]; then
            ln -sf "$tsgcc" "/usr/bin/gcc-${kgcc}"
            echo_step "Symlinked gcc-${kgcc} -> $tsgcc"
         elif command -v gcc &>/dev/null; then
            ln -sf "$(command -v gcc)" "/usr/bin/gcc-${kgcc}"
            echo_step "Symlinked gcc-${kgcc} -> $(command -v gcc)"
         fi
      fi
   fi

   # Verify gcc-N is now available and can handle a basic compile
   if ! command -v "gcc-${kgcc}" &>/dev/null; then
      echo_warn "gcc-${kgcc} still not available after install attempts"
      return 1
   fi
   if ! "gcc-${kgcc}" -E -x c /dev/null -ftrivial-auto-var-init=zero &>/dev/null; then
      echo_warn "gcc-${kgcc} does not support kernel build flags (too old)"
      rm -f "/usr/bin/gcc-${kgcc}" "/usr/local/bin/gcc-${kgcc}"
      return 1
   fi
   # Verify the binary's actual major version matches ${kgcc}. Fedora rawhide
   # ships a transitional 'gcc-13' alias whose --dumpversion reports 16.x; the
   # version skew silently miscompiles DMA fast-paths (observed: PRBS
   # mismatches across every loopback test on fedora:rawhide / gcc 16.0.1
   # pre-release). Kbuild only warns, it doesn't reject; we must reject here.
   local actual_major
   actual_major=$("gcc-${kgcc}" -dumpversion 2>/dev/null | cut -d. -f1)
   if [ -n "$actual_major" ] && [ "$actual_major" != "$kgcc" ]; then
      echo_warn "gcc-${kgcc} reports major version ${actual_major} (expected ${kgcc}) — refusing to use (miscompile risk)"
      rm -f "/usr/bin/gcc-${kgcc}" "/usr/local/bin/gcc-${kgcc}"
      return 1
   fi
   # Check for glibc-incompatible kbuild binaries in the host kernel headers.
   # When the host kernel was built on a newer distro, prebuilt helpers like
   # modpost, objtool, and gendwarfksyms may need a newer glibc. Rebuild
   # them from source with `make scripts` using the container's toolchain.
   # If rebuild fails, stub non-essential tools and fail on essential ones.
   local kbuild="/lib/modules/${HOST_KVER}/build"
   local need_rebuild=0
   if [ -d "$kbuild" ]; then
      while IFS= read -r -d '' binfile; do
         if ldd "$binfile" 2>&1 | grep -q "GLIBC.*not found"; then
            echo_warn "${binfile#$kbuild/} requires newer glibc than this distro provides"
            need_rebuild=1
         fi
      done < <(find -L "$kbuild/scripts" "$kbuild/tools" -type f -executable -print0 2>/dev/null)
   fi
   if [ "$need_rebuild" -eq 1 ]; then
      echo_step "Fixing glibc-incompatible kbuild binaries"
      # libelf-dev and libdw-dev are needed to recompile modpost/gendwarfksyms
      if command -v apt-get &>/dev/null; then
         apt-get install -y libelf-dev libdw-dev zlib1g-dev 2>/dev/null || true
      elif command -v dnf &>/dev/null; then
         dnf install -y elfutils-libelf-devel elfutils-devel zlib-devel 2>/dev/null || true
      fi
      # Recompile essential binaries (modpost, gendwarfksyms) from source
      # using the container's own gcc. These ship as .c files alongside
      # the prebuilt binaries in the kernel headers package.
      # scripts/include/ (hash.h, hashtable.h, list.h, xalloc.h) was
      # added in kernel 6.13; needed by modpost and gendwarfksyms.
      local scripts_inc=""
      if [ -d "$kbuild/scripts/include" ]; then
         scripts_inc="-I$kbuild/scripts/include"
      fi
      local moddir="$kbuild/scripts/mod"
      if [ -f "$moddir/modpost.c" ]; then
         echo_step "Recompiling modpost from source"
         local modpost_src=("$moddir/modpost.c" "$moddir/file2alias.c"
                            "$moddir/sumversion.c" "$moddir/symsearch.c")
         if "gcc-${kgcc}" -o "$moddir/modpost" "${modpost_src[@]}" \
               -I"$moddir" $scripts_inc -lelf -DCONFIG_MODULE_STRIPPED= 2>&1; then
            echo_step "Rebuilt modpost successfully"
         else
            echo_warn "Failed to recompile modpost"
         fi
      fi
      local gdksdir="$kbuild/scripts/gendwarfksyms"
      if [ -f "$gdksdir/gendwarfksyms.c" ]; then
         echo_step "Recompiling gendwarfksyms from source"
         local gdks_srcs=()
         for f in "$gdksdir"/*.c; do gdks_srcs+=("$f"); done
         if "gcc-${kgcc}" -o "$gdksdir/gendwarfksyms" "${gdks_srcs[@]}" \
               -I"$gdksdir" $scripts_inc -ldw -lelf -lz 2>&1; then
            echo_step "Rebuilt gendwarfksyms successfully"
         else
            echo_warn "Failed to recompile gendwarfksyms (non-fatal)"
         fi
      fi
      # Stub non-essential binaries (validation/metadata only)
      local -a stub_ok=(
         "tools/objtool/objtool"
         "tools/bpf/resolve_btfids/resolve_btfids"
         "scripts/insert-sys-cert"
      )
      for relpath in "${stub_ok[@]}"; do
         local binfile="$kbuild/$relpath"
         if [ -x "$binfile" ] && ldd "$binfile" 2>&1 | grep -q "GLIBC.*not found"; then
            mv "$binfile" "${binfile}.host-orig"
            printf '#!/bin/sh\nexit 0\n' > "$binfile"
            chmod +x "$binfile"
            echo_warn "Stubbed $relpath (non-essential)"
         fi
      done
      # Final check: if modpost or gendwarfksyms is still glibc-incompatible, give up
      local modpost_bin="$kbuild/scripts/mod/modpost"
      if [ -x "$modpost_bin" ] && ldd "$modpost_bin" 2>&1 | grep -q "GLIBC.*not found"; then
         echo_warn "modpost still glibc-incompatible after rebuild"
         return 1
      fi
      local gdks_bin="$kbuild/scripts/gendwarfksyms/gendwarfksyms"
      if [ -x "$gdks_bin" ] && ldd "$gdks_bin" 2>&1 | grep -q "GLIBC.*not found"; then
         echo_warn "gendwarfksyms still glibc-incompatible after rebuild"
         return 1
      fi
   fi
   echo_step "Verified gcc-${kgcc} supports kernel build flags"
   return 0
}

# Ubuntu / Debian
if [[ "$DISTRO_ID" == "ubuntu" || "$DISTRO_ID" == "debian" ]]; then
   echo_step "Using apt-get package manager"

   # Retry apt-get update up to 3 times
   UPDATE_OK=0
   for i in 1 2 3; do
      if apt-get update; then
         UPDATE_OK=1
         break
      else
         echo_warn "apt-get update failed (attempt $i/3), retrying in 10s..."
         sleep 10
      fi
   done

   if [ "$UPDATE_OK" -eq 0 ]; then
      echo_fail "apt-get update failed after 3 attempts"
      exit 1
   fi

   # Upgrade (non-fatal - some distros have upgrade issues)
   apt-get upgrade -y || {
      echo_warn "apt-get upgrade failed, continuing anyway..."
   }

   # Install common build tools (non-kernel).  zstd is required when the
   # host kernel was built with CONFIG_MODULE_COMPRESS_ZSTD=y (Azure 6.17+);
   # without it, `dkms install` produces an unreadable .ko.zst and
   # `dkms status` stays at "added" instead of "installed".
   apt-get install -y \
      build-essential \
      make \
      bash \
      sed \
      kmod \
      dkms \
      git \
      pahole \
      zstd

   # Try host kernel headers first; fall back to distro-native generic headers.
   # If headers are already present (e.g. bind-mounted from the host), skip
   # the package install entirely — it would fail on read-only mounts.
   CI_HOST_MATCH=0
   if [ -e "/lib/modules/${HOST_KVER}/build" ]; then
      CI_KVER="$HOST_KVER"
      CI_HOST_MATCH=1
      echo_step "Host kernel headers already present (bind-mount): ${HOST_KVER}"
   else
      apt-get install -y "linux-headers-${HOST_KVER}" || true
      if [ -e "/lib/modules/${HOST_KVER}/build" ]; then
         CI_KVER="$HOST_KVER"
         CI_HOST_MATCH=1
         echo_step "Installed host-matching kernel headers: linux-headers-${HOST_KVER}"
      else
         echo_warn "linux-headers-${HOST_KVER} not available; falling back to distro-native headers"
         if [[ "$DISTRO_ID" == "debian" ]]; then
            apt-get install -y linux-headers-amd64 || apt-get install -y linux-headers-generic
         else
            apt-get install -y linux-headers-generic || apt-get install -y linux-headers-amd64
         fi
         CI_KVER="$(detect_installed_kver)"
         if [ -z "$CI_KVER" ]; then
            echo_fail "No kernel headers found after fallback install"
            exit 1
         fi
         echo_step "Installed distro-native kernel headers for KVER=${CI_KVER}"
      fi
   fi

# Rocky Linux 9 / RHEL 9 / Fedora (dnf)
elif [[ "$DISTRO_ID" == "rocky" || "$DISTRO_ID" == "rhel" || \
        "$DISTRO_ID" == "fedora" ]]; then
   echo_step "Using dnf package manager (Rocky/RHEL/Fedora)"

   # Retry dnf update up to 3 times
   UPDATE_OK=0
   for i in 1 2 3; do
      if dnf update -y && dnf upgrade -y; then
         UPDATE_OK=1
         break
      else
         echo_warn "dnf update failed (attempt $i/3), retrying in 10s..."
         sleep 10
      fi
   done

   if [ "$UPDATE_OK" -eq 0 ]; then
      echo_fail "dnf update failed after 3 attempts"
      exit 1
   fi

   # Common build tools.  zstd is required for kernels built with
   # CONFIG_MODULE_COMPRESS_ZSTD=y (Azure 6.17+) — DKMS post-build compression.
   dnf install -y \
      gcc \
      gcc-c++ \
      make \
      sed \
      kmod \
      git \
      elfutils-libelf-devel \
      zstd

   # Fedora ships BTF tools as 'dwarves' (provides pahole binary)
   if [[ "$DISTRO_ID" == "fedora" ]]; then
      dnf install -y dwarves
   fi

   # Try host kernel headers; fall back to distro-native. If headers are
   # already present (e.g. bind-mounted from the host), skip the package
   # install — it would fail on read-only mounts.
   CI_HOST_MATCH=0
   if [ -e "/lib/modules/${HOST_KVER}/build" ] || [ -d "/usr/src/kernels/${HOST_KVER}" ]; then
      CI_KVER="$HOST_KVER"
      CI_HOST_MATCH=1
      echo_step "Host kernel headers already present (bind-mount): ${HOST_KVER}"
   else
      dnf install -y "kernel-devel-${HOST_KVER}" "kernel-headers-${HOST_KVER}" kernel-modules-core || true
      if [ -e "/lib/modules/${HOST_KVER}/build" ] || [ -d "/usr/src/kernels/${HOST_KVER}" ]; then
         CI_KVER="$HOST_KVER"
         CI_HOST_MATCH=1
         echo_step "Installed host-matching kernel headers: ${HOST_KVER}"
      else
         echo_warn "kernel-devel-${HOST_KVER} not available; falling back to distro-native headers"
         dnf install -y kernel-devel kernel-headers kernel-modules-core
         CI_KVER="$(detect_installed_kver)"
         if [ -z "$CI_KVER" ]; then
            echo_fail "No kernel headers found after fallback install"
            exit 1
         fi
         echo_step "Installed distro-native kernel headers for KVER=${CI_KVER}"
      fi
   fi

else
   echo_fail "Unsupported distribution: $DISTRO_ID $DISTRO_VERSION"
   echo "Supported distributions:"
   echo "  - Ubuntu 22.04, 24.04"
   echo "  - Debian (experimental)"
   echo "  - Rocky Linux 9"
   echo "  - Fedora (rawhide)"
   exit 1
fi

# When using bind-mounted host kernel headers, ensure the container has the
# same GCC major version that built the kernel (kbuild enforces this).
# If the distro's compiler is too old, fall back to distro-native headers.
if [ "$CI_HOST_MATCH" -eq 1 ]; then
   if ! ensure_kernel_gcc; then
      echo_warn "Host kernel toolchain incompatible; falling back to distro-native headers"
      CI_HOST_MATCH=0
      if command -v apt-get &>/dev/null; then
         if [[ "${DISTRO_ID:-}" == "debian" ]]; then
            apt-get install -y linux-headers-amd64 || apt-get install -y linux-headers-generic
         else
            apt-get install -y linux-headers-generic || apt-get install -y linux-headers-amd64
         fi
      elif command -v dnf &>/dev/null; then
         dnf install -y kernel-devel kernel-headers kernel-modules-core
      fi
      CI_KVER="$(detect_installed_kver "$HOST_KVER")"
      if [ -z "$CI_KVER" ]; then
         echo_fail "No kernel headers found after compiler fallback"
         exit 1
      fi
      echo_step "Using distro-native kernel headers for KVER=${CI_KVER}"
   fi
fi

# Export the kernel version we will build against, and whether it matches
# the running host kernel (needed for deciding whether insmod can succeed).
export_ci_var CI_KVER "$CI_KVER"
export_ci_var CI_HOST_MATCH "$CI_HOST_MATCH"

echo_step "Build dependencies ready (KVER=${CI_KVER}, HOST_MATCH=${CI_HOST_MATCH})"
