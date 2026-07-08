#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Sequential CPU/GPU matrix executor for the aes-ci parity VM. Loops
#    over matrix cells and aggregates exit codes with bitwise-OR.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Loops over CELLS[] and invokes scripts/ci-local/run_cell.sh once per cell.
# Exit codes are aggregated via bitwise-OR so one failing cell does NOT
# abort the matrix (fail-fast: false parity with the GitHub workflow).
#
# --phase cpu (default): runs the 5-distro CPU matrix.
# --phase gpu:           runs the 5-distro GPU matrix.
#
# --parallel provisions one KVM per load-test cell and runs all concurrently.
# Each VM has its own kernel, so insmod/rmmod cannot cross-contaminate.
#
# Format of CELLS[] entries: "container|load_test" (pipe-delimited). This
# mirrors the GitHub workflow matrix include: array 1:1 — each row in the
# workflow becomes one entry in CELLS[].
#
# Exit codes:
#   0 = all cells passed
#   N = at least one cell failed (bitwise-OR of all cell exit codes;
#       124/137 hang codes also OR in non-zero)
#   1 = --parallel invoked (stub — deferred to v2)
#   5 = unsupported flag / usage error
# ----------------------------------------------------------------------------

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"

# ----------------------------------------------------------------------------
# Matrix definitions — mirror ci_pipeline.yml include: arrays.
# CPU: ci_pipeline.yml Phase 2 (cpu_test) matrix.
# GPU: ci_pipeline.yml Phase 3 (gpu_test) matrix.
# Both matrices share the same 6-distro shape; only ubuntu:24.04 has
# load_test=true (the others are build-only).
# ----------------------------------------------------------------------------
CPU_CELLS=(
   "ubuntu:24.04|0"
   "ubuntu:24.04|1"
   "ubuntu:22.04|1"
   "ubuntu:26.04|1"
   "rockylinux:9|1"
   "debian:experimental|1"
   "fedora:rawhide|1"
)

GPU_CELLS=(
   "ubuntu:24.04|0"
   "ubuntu:24.04|1"
   "ubuntu:22.04|0"
   "ubuntu:26.04|0"
   "rockylinux:9|0"
   "debian:experimental|1"
   "fedora:rawhide|1"
)

PHASE="cpu"
PARALLEL=0

usage() {
   cat <<EOF
Usage: $0 [OPTIONS]

Execute the CPU or GPU matrix sequentially against the aes-ci parity VM.
Each cell runs scripts/ci-local/run_cell.sh with the cell's container
image, load_test flag, and phase (cpu|gpu).

OPTIONS:
   --phase cpu|gpu    which matrix to run (default: cpu)
   --parallel         provision one VM per load-test cell, run concurrently
   -h, --help         print this message and exit 0

CPU CELLS (${#CPU_CELLS[@]} entries):
EOF
   for cell_spec in "${CPU_CELLS[@]}"; do
      echo "   $cell_spec"
   done
   echo ""
   echo "GPU CELLS (${#GPU_CELLS[@]} entries):"
   for cell_spec in "${GPU_CELLS[@]}"; do
      echo "   $cell_spec"
   done
   cat <<EOF

Exit codes:
   0 = all cells passed
   N = at least one cell failed (bitwise-OR aggregate of cell exit codes)
   3 = VM provisioning failed (parallel mode)
   5 = unsupported flag

See scripts/ci-local/README.md for the full contract.
EOF
}

while [ $# -gt 0 ]; do
   case "$1" in
      --phase)    PHASE="$2"; shift 2; continue ;;
      --parallel) PARALLEL=1 ;;
      -h|--help)  usage; exit 0 ;;
      *)
         echo_fail "Unknown flag: $1"
         usage
         exit 5
         ;;
   esac
   shift
done

if [ "$PHASE" != "cpu" ] && [ "$PHASE" != "gpu" ]; then
   echo_fail "--phase must be cpu or gpu (got: '$PHASE')"
   usage
   exit 5
fi

# Select the cell array based on phase
if [ "$PHASE" = "gpu" ]; then
   CELLS=("${GPU_CELLS[@]}")
else
   CELLS=("${CPU_CELLS[@]}")
fi

# ----------------------------------------------------------------------------
# Parallel mode: provision one VM per load-test cell, run all concurrently.
#
# Each load-test cell gets its own KVM (aes-ci-0, aes-ci-1, ...) with an
# independent kernel, so insmod/rmmod cannot cross-contaminate. Build-only
# cells (load_test=0) share VM slot 0 since they never touch kernel modules.
#
# Resource budget per VM: AES_CI_VM_MEMORY (default 4096 MB) + AES_CI_VM_VCPUS
# (default 4). For N parallel VMs, the host needs N * 4 GB RAM and N * 4 cores.
# Override with AES_CI_PARALLEL_VM_MEMORY / AES_CI_PARALLEL_VM_VCPUS to use
# smaller VMs (e.g. 2048 MB / 2 vCPUs) when running many cells.
# ----------------------------------------------------------------------------
if [ "$PARALLEL" -eq 1 ]; then
   echo_header "Parallel $PHASE matrix (${#CELLS[@]} cells)"

   PAR_MEMORY="${AES_CI_PARALLEL_VM_MEMORY:-${AES_CI_VM_MEMORY:-4096}}"
   PAR_VCPUS="${AES_CI_PARALLEL_VM_VCPUS:-${AES_CI_VM_VCPUS:-4}}"

   # Assign VM slots: load-test cells each get their own VM; build-only share slot 0.
   LOAD_TEST_CELLS=()
   BUILD_ONLY_CELLS=()
   for cell_spec in "${CELLS[@]}"; do
      lt="${cell_spec#*|}"
      if [ "$lt" -eq 1 ]; then
         LOAD_TEST_CELLS+=("$cell_spec")
      else
         BUILD_ONLY_CELLS+=("$cell_spec")
      fi
   done

   NUM_VMS=${#LOAD_TEST_CELLS[@]}
   if [ ${#BUILD_ONLY_CELLS[@]} -gt 0 ] && [ "$NUM_VMS" -eq 0 ]; then
      NUM_VMS=1
   elif [ ${#BUILD_ONLY_CELLS[@]} -gt 0 ]; then
      : # build-only cells will share VM slot 0
   fi

   echo_step "Provisioning $NUM_VMS VMs (${PAR_MEMORY}MB RAM, ${PAR_VCPUS} vCPUs each)"

   # Stage 1: Provision all VMs in parallel
   PROV_PIDS=()
   for i in $(seq 0 $((NUM_VMS - 1))); do
      DOM_NAME="aes-ci-${i}"
      echo_step "Provisioning VM $DOM_NAME"
      (
         export AES_CI_DOMAIN="$DOM_NAME"
         export AES_CI_VM_MEMORY="$PAR_MEMORY"
         export AES_CI_VM_VCPUS="$PAR_VCPUS"
         bash "$SCRIPT_DIR/provision_vm.sh"
      ) &
      PROV_PIDS+=($!)
   done

   # Wait for all VMs to provision.  PROV_FAIL=$((...)) rather than
   # ((PROV_FAIL++)) for the same reason as the VM_SLOT loop below: under
   # `set -e` the post-increment of a zero-valued variable returns exit 1
   # (the pre-increment value) and would abort this waitloop on the first
   # provisioning failure — leaving the remaining pids unreaped and the
   # "N VM(s) failed to provision" message unreachable.
   PROV_FAIL=0
   for pid in "${PROV_PIDS[@]}"; do
      if ! wait "$pid"; then
         PROV_FAIL=$((PROV_FAIL + 1))
      fi
   done
   if [ "$PROV_FAIL" -gt 0 ]; then
      echo_fail "$PROV_FAIL VM(s) failed to provision"
      exit 3
   fi
   echo_step "All $NUM_VMS VMs provisioned"

   # Stage 2: Wait for all VMs to be SSH-reachable
   for i in $(seq 0 $((NUM_VMS - 1))); do
      DOM_NAME="aes-ci-${i}"
      for attempt in $(seq 1 30); do
         VM_IP=$(virsh domifaddr --source agent "$DOM_NAME" 2>/dev/null \
            | awk '/^ enp[0-9]|^ eth[0-9]/ && /ipv4/ {print $4}' | cut -d/ -f1 | head -1)
         if [ -n "$VM_IP" ]; then
            echo_info "$DOM_NAME reachable at $VM_IP"
            break
         fi
         sleep 3
      done
   done

   # Stage 3: Run cells in parallel — one load-test cell per VM
   CELL_PIDS=()
   CELL_LABELS=()
   VM_SLOT=0

   for cell_spec in "${LOAD_TEST_CELLS[@]}"; do
      CONTAINER="${cell_spec%|*}"
      LOAD_TEST="${cell_spec#*|}"
      DOM_NAME="aes-ci-${VM_SLOT}"
      echo_step "Starting cell: $CONTAINER ($PHASE, load_test=$LOAD_TEST) on $DOM_NAME"
      (
         export AES_CI_DOMAIN="$DOM_NAME"
         bash "$SCRIPT_DIR/run_cell.sh" --container "$CONTAINER" --load-test "$LOAD_TEST" --phase "$PHASE"
      ) &
      CELL_PIDS+=($!)
      CELL_LABELS+=("$CONTAINER($PHASE,lt=$LOAD_TEST)@$DOM_NAME")
      # VM_SLOT=$((...)) rather than ((VM_SLOT++)) because the latter
      # returns the pre-increment value — 0 in the first iteration trips
      # set -e and aborts the loop before the remaining cells dispatch.
      VM_SLOT=$((VM_SLOT + 1))
   done

   # Run build-only cells on VM slot 0 (sequentially — they're fast and don't
   # touch kernel modules, but sharing a VM avoids provisioning overhead).
   if [ ${#BUILD_ONLY_CELLS[@]} -gt 0 ]; then
      (
         export AES_CI_DOMAIN="aes-ci-0"
         for cell_spec in "${BUILD_ONLY_CELLS[@]}"; do
            CONTAINER="${cell_spec%|*}"
            bash "$SCRIPT_DIR/run_cell.sh" --container "$CONTAINER" --load-test 0 --phase "$PHASE"
         done
      ) &
      CELL_PIDS+=($!)
      CELL_LABELS+=("build-only-batch@aes-ci-0")
   fi

   # Stage 4: Collect results
   EXIT_OVERALL=0
   for idx in "${!CELL_PIDS[@]}"; do
      set +e
      wait "${CELL_PIDS[$idx]}"
      EXIT_CELL=$?
      set -e
      if [ "$EXIT_CELL" -eq 0 ]; then
         echo_step "${CELL_LABELS[$idx]}: PASS"
      else
         echo_fail "${CELL_LABELS[$idx]}: FAIL (exit $EXIT_CELL)"
      fi
      EXIT_OVERALL=$((EXIT_OVERALL | EXIT_CELL))
   done

   # Stage 5: Cleanup — shut down parallel VMs (keep slot 0 for follow-up work)
   for i in $(seq 1 $((NUM_VMS - 1))); do
      virsh shutdown "aes-ci-${i}" 2>/dev/null || true
   done

   echo_header "$PHASE matrix summary (parallel)"
   if [ "$EXIT_OVERALL" -eq 0 ]; then
      echo_step "All ${#CELLS[@]} cells passed"
   else
      echo_fail "At least one cell failed (aggregate exit $EXIT_OVERALL)"
   fi
   exit $EXIT_OVERALL
fi

# ----------------------------------------------------------------------------
# Sequential mode (default)
# ----------------------------------------------------------------------------
echo_header "Sequential $PHASE matrix (${#CELLS[@]} cells)"

EXIT_OVERALL=0
for cell_spec in "${CELLS[@]}"; do
   CONTAINER="${cell_spec%|*}"
   LOAD_TEST="${cell_spec#*|}"
   echo_step "Starting cell: $CONTAINER ($PHASE, load_test=$LOAD_TEST)"

   set +e
   bash "$SCRIPT_DIR/run_cell.sh" --container "$CONTAINER" --load-test "$LOAD_TEST" --phase "$PHASE"
   EXIT_CELL=$?
   set -e

   if [ "$EXIT_CELL" -eq 0 ]; then
      echo_step "Cell $CONTAINER ($PHASE, load_test=$LOAD_TEST): PASS"
   else
      echo_fail "Cell $CONTAINER ($PHASE, load_test=$LOAD_TEST): FAIL (exit $EXIT_CELL)"
   fi
   EXIT_OVERALL=$((EXIT_OVERALL | EXIT_CELL))
done

# ----------------------------------------------------------------------------
# Summary
# ----------------------------------------------------------------------------
echo_header "$PHASE matrix summary"
if [ "$EXIT_OVERALL" -eq 0 ]; then
   echo_step "All ${#CELLS[@]} cells passed"
else
   echo_fail "At least one cell failed (aggregate exit $EXIT_OVERALL)"
fi
exit $EXIT_OVERALL
