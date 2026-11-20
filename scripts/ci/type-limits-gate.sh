#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Compile-time gate for -Wtype-limits in the kernel driver sources.
#    Source this file, then call type_limits_gate <module dir> [make args].
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Kbuild passes -Wno-type-limits unless W=2 is set, which hides comparisons
# that can never be true, such as an unsigned value tested against "< 0".
# That is how a dead overflow guard in DataDev_Probe and PgpCard_Probe went
# unnoticed: no CI cell has enough devices to reach the guard at run time.
#
# The flag cannot simply be promoted to an error. The kernel's own headers
# (include/linux/bits.h, include/linux/minmax.h) trip it more than a hundred
# times per module, and the count varies with kernel and compiler version.
# The gate therefore builds once with the warning enabled, keeps the output
# out of the CI log, and fails only on warnings whose location is a file in
# this repository. The caller's regular clean and build follow, so the module
# that gets loaded and tested is built exactly as before.
# -----------------------------------------------------------------------------

TYPE_LIMITS_REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"

# type_limits_gate <module dir> [make args...]
# Returns 0 when the module's own sources are free of -Wtype-limits warnings.
type_limits_gate() {
   local dir="$1"
   shift
   local moddir log line file hits=""

   moddir="$(cd "$dir" && pwd -P)"
   log="$(mktemp)"

   # -Wno-error keeps the header warnings non-fatal on kernels configured
   # with CONFIG_WERROR.
   make -C "$dir" clean > /dev/null 2>&1 || true
   if ! make -C "$dir" KCFLAGS="-Wtype-limits -Wno-error=type-limits" "$@" > "$log" 2>&1; then
      cat "$log"
      rm -f "$log"
      echo "FAIL: -Wtype-limits build of ${dir} failed"
      return 1
   fi

   # Kbuild reports module sources either as absolute paths or relative to
   # the module directory, depending on kernel version. Kernel headers are
   # relative to the kernel tree or absolute under it, so they resolve to
   # nothing inside this repository and are skipped.
   while IFS= read -r line; do
      file="${line%%:*}"
      case "$file" in
         /*) ;;
         *)  file="${moddir}/${file}" ;;
      esac
      if [ -e "$file" ] && [[ "$file" == "${TYPE_LIMITS_REPO_ROOT}/"* ]]; then
         hits+="${line}"$'\n'
      fi
   done < <(grep -E '^[^ :]+:[0-9]+:[0-9]+: warning: .*\[-Wtype-limits\]' "$log" | sort -u)
   rm -f "$log"

   if [ -n "$hits" ]; then
      printf '%s' "$hits"
      echo "FAIL: -Wtype-limits warnings in ${dir} sources"
      return 1
   fi
   return 0
}
