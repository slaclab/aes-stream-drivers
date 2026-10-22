#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Retry wrappers for distro package-manager calls in CI containers.
#    Source this file, then use dnf_retry / apt_retry in place of dnf /
#    apt-get with the same arguments.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Mirror failures seen in CI come in two forms:
#   - Transient: TLS/HTTP errors fetching repomd.xml or a package.
#   - Stale metadata: Fedora rawhide replaces packages between composes, so
#     metadata fetched from a lagging mirror can name an RPM that 404s on
#     every mirror. Retrying the download alone can never succeed.
# Each retry therefore refreshes the metadata first (dnf clean metadata /
# apt-get update) and retries the whole transaction with backoff.
#
# A missing package is not retried: several callers probe for optional
# packages (e.g. kernel-devel-$(uname -r)) and expect a fast failure.
#
# Environment variables:
#   PKG_RETRY_ATTEMPTS  Total attempts per call (default: 4)
#   PKG_RETRY_DELAY     Backoff base in seconds; attempt N waits N x this
#                       before retrying (default: 15)
# -----------------------------------------------------------------------------

PKG_RETRY_ATTEMPTS="${PKG_RETRY_ATTEMPTS:-4}"
PKG_RETRY_DELAY="${PKG_RETRY_DELAY:-15}"
PKG_NOT_FOUND_PATTERN='No match for argument|Unable to find a match|Unable to locate package|has no installation candidate|find any package by'

# _pkg_retry REFRESH_CMD CMD [ARGS...]
_pkg_retry() {
   local refresh="$1"
   shift
   local attempt rc errlog
   errlog="$(mktemp)"
   for attempt in $(seq 1 "$PKG_RETRY_ATTEMPTS"); do
      # stderr is shown as usual and also kept for the not-found check.
      # The && / || form keeps a failure from tripping a caller's set -e.
      "$@" 2> >(tee "$errlog" >&2) && rc=0 || rc=$?
      wait $! 2>/dev/null || true
      if [ "$rc" -eq 0 ] || grep -qE "$PKG_NOT_FOUND_PATTERN" "$errlog"; then
         break
      fi
      if [ "$attempt" -lt "$PKG_RETRY_ATTEMPTS" ]; then
         echo "WARN: '$*' failed (exit $rc, attempt $attempt/$PKG_RETRY_ATTEMPTS); refreshing metadata and retrying in $((attempt * PKG_RETRY_DELAY))s" >&2
         sleep $((attempt * PKG_RETRY_DELAY))
         $refresh > /dev/null 2>&1 || true
      fi
   done
   rm -f "$errlog"
   return "$rc"
}

dnf_retry() { _pkg_retry "dnf clean metadata" dnf "$@"; }
apt_retry() { _pkg_retry "apt-get update" apt-get "$@"; }
