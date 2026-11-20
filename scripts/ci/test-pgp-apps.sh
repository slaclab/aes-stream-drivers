#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Argument parsing regression test for the pgpcard applications. Needs no
#    PGP hardware and no loaded module, so it runs on every CI cell.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Every pgpcard app once declared its value options with OPTION_ARG_OPTIONAL,
# which makes glibc argp require the value to be attached ("-s5000"). With a
# space ("-s 5000") the handler received NULL: strtol(NULL) segfaulted and a
# spaced "-p PATH" was parsed as a positional argument. Each app is invoked
# with "--help" and then with every value option given in spaced form plus a
# device path that cannot exist. A correct parse reaches the open() call and
# fails with "Error opening <path>" and exit code 1.
#
# Assumes the applications are already built by build-pgp.sh
#
# Exit codes: number of failed checks (0 = all passed)
# ----------------------------------------------------------------------------

set -uo pipefail

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo_step() { echo -e "${GREEN}==>${NC} $1"; }
echo_fail() { echo -e "${RED}FAIL:${NC} $1"; }

APP_BIN="${APP_BIN:-pgpcard/app/bin}"
NODEV="/nonexistent/pgpcard_argtest"

FAILED=0

# check_help <app>
#   "--help" must exit 0.
check_help() {
   local app="$1"
   if "${APP_BIN}/${app}" --help > /dev/null 2>&1; then
      echo "  PASS: ${app} --help"
   else
      echo_fail "${app} --help exited $?"
      FAILED=$((FAILED + 1))
   fi
}

# check_args <app> <args...>
#   Spaced value options must be consumed as values: the app reaches open(),
#   prints "Error opening <path>" and exits 1. A segfault (139) or an argp
#   usage error (64) means an option value was not consumed.
check_args() {
   local app="$1"
   shift
   local out rc
   out="$("${APP_BIN}/${app}" "$@" 2>&1)"
   rc=$?
   if [ "$rc" -eq 1 ] && echo "$out" | grep -q "Error opening ${NODEV}"; then
      echo "  PASS: ${app} $*"
   else
      echo_fail "${app} $* (exit ${rc})"
      echo "$out" | sed 's/^/        /'
      FAILED=$((FAILED + 1))
   fi
}

echo_step "Checking --help for every pgpcard application"
for app in pgpGetStatus pgpLoopTest pgpPromLoad pgpPromVerify pgpRead \
           pgpSetData pgpSetDebug pgpSetLoop pgpWrite; do
   check_help "$app"
done

echo_step "Checking spaced value options reach open()"
check_args pgpGetStatus  -p "$NODEV"
check_args pgpLoopTest   -p "$NODEV" -v 0x3 -s 5000 -t 10
check_args pgpPromLoad   -p "$NODEV" prom.mcs
check_args pgpPromVerify -p "$NODEV" prom.mcs
check_args pgpRead       -p "$NODEV" -l 0x3
check_args pgpSetData    -p "$NODEV" -l 0x1 0
check_args pgpSetDebug   -p "$NODEV" 1
check_args pgpSetLoop    -p "$NODEV" -l 0x1 0
check_args pgpWrite      -p "$NODEV" -s 1000 -c 1 0 0

if [ "$FAILED" -eq 0 ]; then
   echo_step "All pgpcard argument parsing checks passed"
else
   echo_fail "${FAILED} pgpcard argument parsing check(s) failed"
fi
exit "$FAILED"
