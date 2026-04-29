#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Execute one CI matrix cell on the aes-ci parity VM. Drives the
#    scripts/ci/*.sh chain inside a docker container over SSH.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Drives the GitHub-runner CPU or GPU CI chain
#   (install-deps -> build-cpu -> [load-modules -> test -> unload -> dkms,
#    if load_test=1] -> check-dmesg)
# inside a docker container running on the aes-ci parity guest. Every
# scripts/ci/*.sh file is invoked VERBATIM by exact path — the
# harness's only job is to set up cwd + env + virtiofs bind-mount; the
# scripts do the CI work.
#
# Every docker invocation carries both --privileged AND --security-opt
# label=disable on the same line — RHEL S3DF SELinux otherwise blocks
# init_module at the LSM hook.
#
# Timeout hierarchy:
#   outer: host-side `timeout --kill-after=30s ${CELL_TIMEOUT}s` wraps
#          the ssh+docker run. Default 900s (15 min). Configurable via
#          AES_CI_CELL_TIMEOUT_SEC.
#   mid:   docker signal propagation to the container on SIGTERM.
#   inner: scripts/ci/load-modules-cpu.sh's per-insmod 120s timeout.
#
# The 4 load-test-gated steps (load, test, unload, dkms) only
# execute when --load-test 1; build-only cells (--load-test 0) run just
# install-deps + build-cpu + check-dmesg, matching the workflow's
# `if: matrix.load_test` guards.
#
# Log layout:
#   logs/<ts>/<sanitized-container>/cpu-<mode>.log
# where ts=YYYYMMDD-HHMMSS, sanitized strips : and / from container name,
# mode is "build-only" or "load-test".
#
# Flags:
#   --container IMG     docker image (e.g., ubuntu:24.04) — REQUIRED
#   --load-test 0|1     whether to run load/test/unload/dkms steps — REQUIRED
#   --phase cpu|gpu     which CI script chain to run (default: cpu)
#   --log-dir PATH      override log directory (default: logs/<ts>/<sanitized>/)
#   -h, --help          print usage and exit 0
#
# Environment variable contract:
#   AES_CI_DOMAIN              (default: aes-ci) libvirt domain name
#   AES_CI_GUEST_MOUNT         (default: /mnt/aes-stream-drivers) in-guest
#                              virtiofs mount path
#   AES_CI_CELL_TIMEOUT_SEC    (default: 900 = 15 min) outer wall-clock
#                              budget per cell
#   AES_CI_CACHE_DIR           used only via AES_CI_CACHE_DIR_RESOLVED from
#                              lib/common.sh (SSH key location)
#
# Exit codes:
#   0   = cell passed (all 7 or 3 scripts/ci/*.sh steps exit 0)
#   N   = first non-zero exit from the chained scripts (propagated through
#         docker -> ssh -> timeout -> ${PIPESTATUS[0]})
#   124 = outer timeout fired SIGTERM (cell wall-clock exceeded)
#   137 = outer timeout fired SIGKILL (kill-after tripped)
#   3   = infrastructure error (guest unreachable, virtiofs mount missing,
#         SSH auth failed)
#   5   = unsupported flag / missing required flag / usage error
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
GUEST_MOUNT="${AES_CI_GUEST_MOUNT:-/mnt/aes-stream-drivers}"
CELL_TIMEOUT="${AES_CI_CELL_TIMEOUT_SEC:-900}"
SSH_KEY="$CACHE_DIR/id_ed25519"

CONTAINER=""
LOAD_TEST=""
PHASE="cpu"
LOG_DIR=""
TS="$(date +%Y%m%d-%H%M%S)"

usage() {
   cat <<EOF
Usage: $0 --container IMG --load-test {0|1} [--phase cpu|gpu] [--log-dir PATH]

Execute one CI matrix cell on the aes-ci parity VM. The scripts/ci/*.sh
chain runs verbatim inside a docker container over SSH.

Required:
   --container IMG     docker image (e.g., ubuntu:24.04)
   --load-test 0|1     0=build-only, 1=also run load/test/unload/dkms

Optional:
   --phase cpu|gpu     which CI script chain to run (default: cpu)
   --log-dir PATH      override log dir (default: logs/<ts>/<sanitized>/)

Environment:
   AES_CI_DOMAIN             libvirt domain (default: aes-ci)
   AES_CI_GUEST_MOUNT        in-guest mount path (default: /mnt/aes-stream-drivers)
   AES_CI_CELL_TIMEOUT_SEC   outer timeout (default: 900)

Exit codes: 0 pass, N script-chain exit, 124/137 outer timeout, 3 infra, 5 usage.
EOF
}

# ----------------------------------------------------------------------------
# Flag parsing — while/case/shift, matches hang_repro.sh and provision_vm.sh style.
# ----------------------------------------------------------------------------
while [ $# -gt 0 ]; do
   case "$1" in
      --container)  CONTAINER="$2"; shift 2 ;;
      --load-test)  LOAD_TEST="$2"; shift 2 ;;
      --phase)      PHASE="$2"; shift 2 ;;
      --log-dir)    LOG_DIR="$2"; shift 2 ;;
      -h|--help)    usage; exit 0 ;;
      *)
         echo_fail "Unknown flag: $1"
         usage
         exit 5
         ;;
   esac
done

if [ -z "$CONTAINER" ]; then
   echo_fail "--container is required"
   usage
   exit 5
fi
if [ "$LOAD_TEST" != "0" ] && [ "$LOAD_TEST" != "1" ]; then
   echo_fail "--load-test must be 0 or 1 (got: '$LOAD_TEST')"
   usage
   exit 5
fi
if [ "$PHASE" != "cpu" ] && [ "$PHASE" != "gpu" ]; then
   echo_fail "--phase must be cpu or gpu (got: '$PHASE')"
   usage
   exit 5
fi

# ----------------------------------------------------------------------------
# Log layout: logs/<ts>/<sanitized>/cpu-<mode>.log
# Sanitize rules match the CI workflow's SANITIZED pattern (: -> -, / -> -)
# ----------------------------------------------------------------------------
SANITIZED="$(echo "$CONTAINER" | sed 's/:/-/g; s|/|-|g')"
if [ "$LOAD_TEST" = "1" ]; then
   MODE="load-test"
else
   MODE="build-only"
fi
if [ -z "$LOG_DIR" ]; then
   LOG_DIR="logs/$TS/$SANITIZED"
fi
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/$PHASE-$MODE.log"

echo_header "Cell: $CONTAINER ($PHASE, load_test=$LOAD_TEST)"
echo_info "Log file:        $LOG_FILE"
echo_info "Outer timeout:   ${CELL_TIMEOUT}s"
echo_info "Guest mount:     $GUEST_MOUNT"

# ----------------------------------------------------------------------------
# Guest IP discovery — enp*/eth* filter skips lo/docker0/virbr0.
# ----------------------------------------------------------------------------
GUEST_IP=""
for _ in 1 2 3 4 5; do
   GUEST_IP=$(virsh domifaddr --source agent "$DOM" 2>/dev/null \
      | awk '/^ enp[0-9]|^ eth[0-9]/ && /ipv4/ {print $4}' | cut -d/ -f1 | head -1)
   if [ -n "$GUEST_IP" ]; then break; fi
   sleep 2
done
if [ -z "$GUEST_IP" ]; then
   echo_fail "Cannot reach $DOM guest — is it running? ('virsh list' to check)"
   exit 3
fi

SSH_OPTS="-i $SSH_KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5"
SSH_CMD="ssh $SSH_OPTS ubuntu@$GUEST_IP"

echo_info "Guest IP: $GUEST_IP"

# ----------------------------------------------------------------------------
# Virtiofs mount sanity.
# systemd mount ordering vs libvirtd's virtiofsd lifecycle isn't guaranteed.
# Retry the mount once; if that fails, exit 3 (not a cell failure — infra).
# ----------------------------------------------------------------------------
if ! $SSH_CMD "test -f $GUEST_MOUNT/scripts/ci/install-deps.sh" 2>/dev/null; then
   echo_warn "Virtiofs mount not populated at $GUEST_MOUNT; attempting remount"
   if ! $SSH_CMD "sudo mount -a || sudo mount -t virtiofs aes-stream-drivers $GUEST_MOUNT" 2>/dev/null; then
      echo_fail "Cannot mount virtiofs share at $GUEST_MOUNT on guest $DOM."
      echo_fail "  Possible causes:"
      echo_fail "    - VM without virtiofs: run bash scripts/ci-local/enable_virtiofs.sh"
      echo_fail "      or re-provision: ./scripts/run_ci_parity.sh --reset"
      echo_fail "    - libvirt-qemu (uid 64055) cannot read AES_CI_REPO_SOURCE (NFS root_squash)"
      echo_fail "      — bind-mount to /var/tmp/aes-ci-mirror, or set AES_CI_REPO_SOURCE to a local-disk path"
      exit 3
   fi
   if ! $SSH_CMD "test -f $GUEST_MOUNT/scripts/ci/install-deps.sh" 2>/dev/null; then
      echo_fail "Virtiofs remount succeeded but $GUEST_MOUNT/scripts/ci/install-deps.sh still missing"
      exit 3
   fi
fi

# ----------------------------------------------------------------------------
# Build the in-container step chain.
#
# The virtiofs mount from the host may be read-only to container root
# (NFS root_squash on the host propagates through virtiofs). To match
# GitHub Actions' writable checkout, we mount the virtiofs share as
# /src (read-only source) and copy to /work (writable) before building.
# The copy adds ~2-3s per cell but avoids permission failures in kbuild.
#
# Every scripts/ci/*.sh is invoked by exact path — no copies, no
#         shims, no parameter substitution. The harness provides cwd + env;
#         the scripts do the work.
# The final check-dmesg.sh runs unconditionally (workflow: `if: always()`);
# on build-only cells it gracefully skips when /tmp/ci_dmesg_marker is absent.
#
# Single bash -c keeps the /tmp side-channel (ci_kver, ci_dmesg_marker) in
# the same container between steps.
# ----------------------------------------------------------------------------
COPY_STEP='cp -a /src/. /work/ && '

if [ "$PHASE" = "gpu" ]; then
   if [ "$LOAD_TEST" = "1" ]; then
      STEPS="${COPY_STEP}"'bash scripts/ci/install-deps.sh && \
             bash scripts/ci/build-gpu.sh && \
             { bash scripts/ci/load-modules-gpu.sh && bash scripts/ci/test-gpu.sh; rc=$?; \
               bash scripts/ci/unload-modules-gpu.sh; \
               [ $rc -eq 0 ] && bash scripts/ci/test-dkms-gpu.sh; \
               exit $rc; } ; rc=$? ; \
             bash scripts/ci/check-dmesg.sh ; \
             exit $rc'
   else
      STEPS="${COPY_STEP}"'bash scripts/ci/install-deps.sh && \
             bash scripts/ci/build-gpu.sh ; rc=$? ; \
             bash scripts/ci/check-dmesg.sh ; \
             exit $rc'
   fi
else
   if [ "$LOAD_TEST" = "1" ]; then
      STEPS="${COPY_STEP}"'bash scripts/ci/install-deps.sh && \
             bash scripts/ci/build-cpu.sh && \
             { bash scripts/ci/load-modules-cpu.sh && bash scripts/ci/test-cpu.sh; rc=$?; \
               bash scripts/ci/unload-modules-cpu.sh; \
               [ $rc -eq 0 ] && bash scripts/ci/test-dkms-cpu.sh; \
               exit $rc; } ; rc=$? ; \
             bash scripts/ci/check-dmesg.sh ; \
             exit $rc'
   else
      STEPS="${COPY_STEP}"'bash scripts/ci/install-deps.sh && \
             bash scripts/ci/build-cpu.sh ; rc=$? ; \
             bash scripts/ci/check-dmesg.sh ; \
             exit $rc'
   fi
fi

# ----------------------------------------------------------------------------
# The docker run — both --privileged and --security-opt label=disable
# on the same line. PIPESTATUS[0] recovers timeout/ssh/docker exit
# through the tee pipe.
# ----------------------------------------------------------------------------
echo_step "Starting cell (privileged + label=disable)"

set +e
timeout --kill-after=30s "${CELL_TIMEOUT}s" \
   $SSH_CMD "docker run --rm --privileged --security-opt label=disable \
      -v $GUEST_MOUNT:/src:ro \
      -v /lib/modules:/lib/modules \
      -v /usr/src:/usr/src \
      -w /work \
      $CONTAINER bash -c '$STEPS'" \
   2>&1 | tee "$LOG_FILE"
RC=${PIPESTATUS[0]}
set -e

# ----------------------------------------------------------------------------
# Classify exit code (emit diagnostic messaging; do NOT capture .diag bundle
# — the artifact collector is separate from the cell runner).
# ----------------------------------------------------------------------------
if [ "$RC" -eq 0 ]; then
   echo_step "Cell $CONTAINER ($PHASE-$MODE): PASS (log: $LOG_FILE)"
   exit 0
fi

if [ "$RC" -eq 124 ] || [ "$RC" -eq 137 ]; then
   echo_fail "Cell $CONTAINER ($PHASE-$MODE): HANG — outer ${CELL_TIMEOUT}s timeout fired (exit $RC)"
   echo_fail "  Diagnostic: the cell exceeded its wall-clock budget."
   echo_fail "  Log tail (last 40 lines):"
   tail -40 "$LOG_FILE" 2>/dev/null | sed 's/^/    /'
   echo_fail "  Next steps:"
   echo_fail "    - Inspect full log: $LOG_FILE"
   echo_fail "    - If a stuck insmod is suspected, guest may be wedged;"
   echo_fail "      re-provision: ./scripts/run_ci_parity.sh --reset"
   echo_fail "    - Artifact collector will auto-collect sysrq-t/proc-stacks here."
   exit $RC
fi

echo_fail "Cell $CONTAINER ($PHASE-$MODE): FAIL (exit $RC)"
echo_fail "  Log: $LOG_FILE"
echo_fail "  Log tail (last 40 lines):"
tail -40 "$LOG_FILE" 2>/dev/null | sed 's/^/    /'
exit $RC
