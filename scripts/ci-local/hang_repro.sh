#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    CPU insmod-chain hang reproducer. Drives the exact CI insmod chain
#    inside a matching docker container on the aes-ci parity VM.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Drives the exact CPU CI chain that hangs on the GitHub hosted runner,
# inside a matching docker container running on the aes-ci parity VM:
#
#   step 1: install package deps (scripts/ci/install-deps).
#   step 2: build CPU kernel modules (scripts/ci/build-cpu).
#   step 3: load the modules (scripts/ci/load-modules-cpu).
#
# Every docker invocation carries both the privileged flag AND the
# SELinux-label-disable flag (RHEL S3DF hosts with enforcing
# SELinux otherwise block init_module() at the LSM hook).
#
# Outer timeout: 180s wall-clock on the host side (belt). The in-container
# 120s per-insmod timeout is the suspenders. If the outer timeout fires
# (exit 124 SIGTERM or 137 SIGKILL), the diagnostic-capture helper dumps
# diagnostics before this script returns.
#
# After a hang is captured, the guest may be permanently wedged (D-state
# insmod cannot be killed with SIGKILL). Document the hang, exit, and let
# the contributor re-provision via `run_ci_parity.sh --reset`.
#
# Environment variable contract — one env var per config line in the config
# block below (cache dir, libvirt domain, outer timeout, guest-side repo
# path). See the declarations for the default value of each.
#
# Exit codes:
#   0 = insmod chain completed (no hang — unexpected but reported as success)
#   1 = hang reproduced AND diagnostics captured
#   2 = hang reproduced but diagnostic capture itself failed
#   3 = infrastructure error (no SSH, no VM, repo missing in guest, etc.)
# ----------------------------------------------------------------------------

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"

# ----------------------------------------------------------------------------
# Config — every tunable has an env-var contract documented in the header.
# ----------------------------------------------------------------------------
CACHE_DIR="$AES_CI_CACHE_DIR_RESOLVED"
DOM="${AES_CI_DOMAIN:-aes-ci}"
OUTER_TIMEOUT="${AES_CI_OUTER_TIMEOUT:-180}"
REPO_IN_GUEST="${AES_CI_REPO_IN_GUEST:-/home/ubuntu/aes-stream-drivers}"
SSH_KEY="$CACHE_DIR/id_ed25519"

DIAG=".diag/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$DIAG"

echo_header "Hang reproduction on aes-ci guest"

# ----------------------------------------------------------------------------
# Guest IP discovery — via qemu-guest-agent. Interface filter enp*/eth*
# skips lo and docker0.
# ----------------------------------------------------------------------------
GUEST_IP=""
for _ in 1 2 3 4 5; do
   GUEST_IP=$(virsh domifaddr --source agent "$DOM" 2>/dev/null | awk '/^ enp[0-9]|^ eth[0-9]/ && /ipv4/ {print $4}' | cut -d/ -f1 | head -1)
   if [ -n "$GUEST_IP" ]; then break; fi
   sleep 2
done
if [ -z "$GUEST_IP" ]; then
   echo_fail "Cannot reach aes-ci guest — is it running? ('virsh list' to check)"
   exit 3
fi

SSH_OPTS="-i $SSH_KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5"
SSH_CMD="ssh $SSH_OPTS ubuntu@$GUEST_IP"

echo_info "Guest IP: $GUEST_IP"
echo_info "Repo path in guest: $REPO_IN_GUEST"
echo_info "Outer timeout: ${OUTER_TIMEOUT}s"

# ----------------------------------------------------------------------------
# Repo-path sanity — requires the contributor to rsync the checkout
# into the guest manually (or use virtiofs). Fail fast with the exact
# rsync command needed if the tree is not present.
# ----------------------------------------------------------------------------
if ! $SSH_CMD "test -d $REPO_IN_GUEST" 2>/dev/null; then
   echo_fail "Repository not found in guest at $REPO_IN_GUEST"
   echo_fail "  Copy the repo into the guest manually, or use virtiofs."
   echo_fail ""
   echo_fail "  From this host, run:"
   echo_fail "    rsync -av -e 'ssh $SSH_OPTS' $PWD/ ubuntu@$GUEST_IP:$REPO_IN_GUEST/"
   exit 3
fi

# ----------------------------------------------------------------------------
# Hang reproduction — three layers of timeout enclose the hang:
#   outer: host-side wall-clock (this script, ${OUTER_TIMEOUT}s)
#   mid:   docker's own signal-propagation to the container
#   inner: per-insmod 120s timeout in the in-container loader
# SELinux label=disable is enforced on the docker command line below.
# ----------------------------------------------------------------------------
echo_step "Starting CPU insmod chain in ubuntu container (outer ${OUTER_TIMEOUT}s)"

set +e
timeout --kill-after=5s "${OUTER_TIMEOUT}s" \
   $SSH_CMD "docker run --rm --privileged --security-opt label=disable \
      -v $REPO_IN_GUEST:/work \
      -w /work \
      ubuntu:24.04 bash -c 'bash scripts/ci/install-deps.sh && \
                            bash scripts/ci/build-cpu.sh && \
                            bash scripts/ci/load-modules-cpu.sh'" \
   > "$DIAG/repro.log" 2>&1
RC=$?
set -e

# ----------------------------------------------------------------------------
# Classify exit code.
#   0       = insmod chain completed (no hang this run)
#   124/137 = outer timeout fired (SIGTERM=124, SIGKILL=137 after kill-after)
#   other   = some non-timeout failure (build error, ssh disconnect, ...)
# ----------------------------------------------------------------------------
if [ "$RC" -eq 0 ]; then
   echo_step "Insmod chain completed without hanging (no reproduction this run)."
   echo_info "  Full log at: $DIAG/repro.log"
   echo_info "  If this is a persistent pattern, the hang may have been fixed —"
   echo_info "  or the guest kernel drifted further from the GitHub runner."
   exit 0
fi

if [ "$RC" -eq 124 ] || [ "$RC" -eq 137 ]; then
   # 124 = outer timeout fired SIGTERM
   # 137 = outer timeout fired SIGKILL (kill-after=5s after 124)
   echo_warn "Hang detected (outer timeout, exit $RC). Capturing diagnostics..."

   # Hand off to the diagnostic capture helper.
   if bash "$SCRIPT_DIR/capture_diag.sh" "$DIAG"; then
      echo_fail "Hang reproduced. Diagnostic triple captured in $DIAG/"
      echo_fail "  Files: $(ls "$DIAG" 2>/dev/null | tr '\n' ' ')"
      echo_fail ""
      echo_fail "  Next steps:"
      echo_fail "   1. Read $DIAG/proc-stacks.log to see WHERE insmod is stuck."
      echo_fail "   2. Read $DIAG/dmesg.log for RCU stall / lockdep reports."
      echo_fail "   3. Document root cause analysis."
      echo_fail "   4. The guest may now be permanently wedged (D-state insmod)—"
      echo_fail "      run ./scripts/run_ci_parity.sh --reset before iterating."
      exit 1
   else
      echo_fail "Hang detected but diagnostic capture failed — see output above."
      exit 2
   fi
fi

# Any other non-zero exit: not a hang, some other failure (build error, SSH
# disconnect, etc.). Report but don't capture (it's not a hang).
echo_fail "Repro script exited $RC — neither success (0) nor timeout (124/137)."
echo_fail "  Tail of repro.log:"
tail -40 "$DIAG/repro.log" 2>/dev/null | sed 's/^/    /'
exit 3
