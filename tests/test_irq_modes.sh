#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    IRQ behavior sweep test. Sweeps cfgIrqHold and cfgIrqDis module
#    parameters, verifying PRBS integrity and clean dmesg for each mode.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Sweeps three IRQ-related module parameters: cfgIrqHold=1 (minimum
# coalescing), cfgIrqHold=100000 (heavy coalescing), and cfgIrqDis=1
# (polled mode). Each sub-test reloads datadev, runs dmaLoopTest for 5s,
# and verifies PRBS integrity + clean dmesg.
#
# Requires root/sudo -- this test reloads kernel modules.
#
# Environment variables:
#   DEV          Device path (default: /dev/datadev_0)
#   APP_BIN      Binary directory (default: data_dev/app/bin)
#   DATADEV_KO   Path to datadev.ko (default: data_dev/driver/datadev.ko)
#   SIZE         dmaLoopTest frame size (default: 32768)
#   TIMEOUT_SEC  Module init timeout (default: 15)
# ----------------------------------------------------------------------------

set -uo pipefail

DEV="${DEV:-/dev/datadev_0}"
APP_BIN="${APP_BIN:-data_dev/app/bin}"
DATADEV_KO="${DATADEV_KO:-data_dev/driver/datadev.ko}"
EMULATOR_KO="${EMULATOR_KO:-emulator/driver/datadev_emulator.ko}"
SIZE="${SIZE:-32768}"
TIMEOUT_SEC="${TIMEOUT_SEC:-15}"
INSMOD_TIMEOUT_SEC="${INSMOD_TIMEOUT_SEC:-120}"

# Require root/sudo capability. Skip gracefully if neither is available so
# this script can be dry-run on unprivileged hosts without breaking CI.
if [ "$(id -u)" -ne 0 ] && ! sudo -n true 2>/dev/null; then
   echo "SKIP: test_irq_modes.sh requires root/sudo (no TTY sudo allowed)"
   exit 0
fi

# sudo prefix helper -- empty when already root.
if [ "$(id -u)" -eq 0 ]; then
   SUDO=""
else
   SUDO="sudo"
fi

# Sanity check module file.
if [ ! -f "$DATADEV_KO" ]; then
   echo "FAIL: module file not found: $DATADEV_KO"
   exit 1
fi

FAILED=0

echo "=== IRQ behavior sweep test ==="

# run_irq_cycle LABEL [insmod_params...]
# Reloads datadev with the given parameters, runs a short dmaLoopTest, checks
# PRBS and dmesg. Sets CYCLE_FAIL=1 on any error.  Also stashes the most
# recent "Probe: using <kind> interrupts" line in CYCLE_PROBE_LINE so the
# outer mode sweep can verify the cascade selection without doing an extra
# insmod (which would compound DMA-buffer allocation pressure across the
# 3-mode sweep and trip the cell's later load/unload tests with -ENOMEM).
run_irq_cycle() {
   local label="$1"
   shift
   local insmod_params="$*"
   CYCLE_PROBE_LINE=""

   echo "--- IRQ sub-test: $label ---"
   DMESG_BEFORE=$($SUDO dmesg | wc -l)
   CYCLE_FAIL=0

   # Unload datadev to establish known state.
   $SUDO rmmod datadev 2>/dev/null || true
   for _ in $(seq 1 15); do [ ! -e "$DEV" ] && break; sleep 0.5; done

   # Reload with IRQ params.
   # shellcheck disable=SC2086
   timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" $insmod_params

   # Wait for driver to reach live state.
   timeout $TIMEOUT_SEC bash -c "until [ \"\$(cat /sys/module/datadev/initstate 2>/dev/null)\" = live ]; do sleep 0.5; done" || {
      echo "[FAIL] irq_modes $label -- datadev did not reach live state"
      CYCLE_FAIL=1
      return
   }

   # Ensure /dev node exists (Docker containers may lack udev).
   if [ ! -e "$DEV" ]; then
      DATADEV_MAJOR=$(awk '$2 == "datadev_0" { print $1 }' /proc/devices)
      $SUDO mknod "$DEV" c "$DATADEV_MAJOR" 0
      $SUDO chmod 666 "$DEV"
   fi
   $SUDO chmod 666 "$DEV"

   # Capture the "Probe: using <kind> interrupts" line BEFORE dmaLoopTest
   # runs with cfgDebug=1 -- the per-frame Process/MapReturn lines flood
   # dmesg fast enough to evict the probe line out of any fixed tail window
   # within ~5 seconds. Scan the dmesg delta produced by THIS cycle (from
   # DMESG_BEFORE, captured at cycle start before the rmmod/insmod) rather
   # than a fixed `tail -n N`: on fast/noisy kernels (e.g. 7.0.0) the probe
   # line is emitted during insmod init and pushed well past 80 lines before
   # this runs, so a fixed window captured an empty string. The delta is
   # bounded to this cycle, so the last match is unambiguously the current
   # probe line. The outer mode-sweep reads this back via CYCLE_PROBE_LINE.
   CYCLE_PROBE_LINE=$($SUDO dmesg | tail -n "+$((DMESG_BEFORE + 1))" | \
                      grep -oE 'Probe: using [A-Za-z0-9-]+([ -]+[A-Za-z]+)* interrupts' | \
                      tail -1 || echo "")

   sleep 2

   # Run dmaLoopTest; let it run until timeout (rc=124 ok).
   local TMPFILE
   TMPFILE=$(mktemp)
   timeout 30 "$APP_BIN/dmaLoopTest" -p "$DEV" -m 0 -s "$SIZE" > "$TMPFILE" 2>&1
   local LOOP_RC=$?

   # Sanity-check the dmaLoopTest exit code: 0 (clean exit) and 124
   # (timeout killed it) are both "no fatal error"; anything else means
   # the process itself crashed before a grep-able error string made it
   # to stdout.
   if [ "$LOOP_RC" -ne 0 ] && [ "$LOOP_RC" -ne 124 ]; then
      echo "[FAIL] irq_modes $label -- dmaLoopTest exited $LOOP_RC"
      CYCLE_FAIL=1
   fi

   # dmaLoopTest returns 0 even when a worker thread hit Read Error /
   # Write Error / Error opening device (see runWrite/runRead in
   # data_dev/app/src/dmaLoopTest.cpp), so $LOOP_RC alone can't
   # distinguish pass from fail — add an explicit error-string grep.
   if grep -qE "Read Error|Write Error|Error opening device" "$TMPFILE"; then
      echo "[FAIL] irq_modes $label -- dmaLoopTest worker thread reported an error"
      grep -E "Read Error|Write Error|Error opening device" "$TMPFILE" | head -5
      CYCLE_FAIL=1
   fi

   # PRBS integrity check.
   if grep -q "Prbs mismatch" "$TMPFILE"; then
      echo "[FAIL] irq_modes $label -- PRBS mismatch"
      grep "Prbs mismatch" "$TMPFILE" | head -5
      CYCLE_FAIL=1
   fi

   # NOTE: no RxCount/TxCount throughput assertion by design. The retry-once
   # wrapper (run_irq_subtest) + the dmesg oops/panic/BUG:/WARNING: scan below
   # together catch a genuinely broken IRQ mode; a brief polled-mode starvation
   # that produces few transfers is acceptable as long as the kernel is clean
   # and PRBS integrity (above) is intact.

   # Kernel error check against dmesg delta. Use printf '%s\n' instead of
   # echo for variable data: echo's option/escape parsing is implementation-
   # defined for leading -n/-e or backslash sequences; printf is the
   # defensive default (matches scripts/ci/check-dmesg.sh).
   DMESG_DELTA=$($SUDO dmesg | tail -n "+$((DMESG_BEFORE + 1))")
   if printf '%s\n' "$DMESG_DELTA" | grep -iE 'oops|panic|BUG:|WARNING:'; then
      echo "[FAIL] irq_modes $label -- kernel error in dmesg"
      CYCLE_FAIL=1
   fi

   rm -f "$TMPFILE"
}

# Sub-test 1: minimum IRQ coalescing (cfgIrqHold=1).
# Subtest runner with up to 2 attempts.  Stochastic early-frame PRBS
# mismatches have been observed on GitHub Actions runners (Azure kernel
# 6.17, under contention) even when the local KVM harness on the same
# kernel passes reliably.  Retrying catches the rare init-window race
# without masking a genuine regression: a true bug fails twice.
run_irq_subtest() {
   local label="$1"
   shift
   local params="$*"
   local attempt
   for attempt in 1 2; do
      # shellcheck disable=SC2086
      run_irq_cycle "$label" $params
      if [ "$CYCLE_FAIL" -eq 0 ]; then
         echo "[PASS] irq_modes $label"
         return 0
      fi
      if [ "$attempt" -lt 2 ]; then
         echo "[RETRY] irq_modes $label (attempt $attempt failed, retrying once)"
      fi
   done
   FAILED=$((FAILED + 1))
   return 1
}

# ----------------------------------------------------------------------------
# Outer sweep: emu_irq_mode in {intx, msi, msix}.
#
# For each emulator IRQ mode, reload datadev_emulator with the requested mode
# (so cfg_space advertises the matching capability), then run the existing
# three cfgIrqHold/polled subtests against that emulator.  After the
# subtests, scrape dmesg to assert datadev's probe selected the correct
# cascade branch -- a regression in data_dev_top.c's pci_alloc_irq_vectors
# call would silently land on a different branch and pass the cfg subtests
# but fail the dmesg gate below.
#
# Hosts without a built emulator (EMULATOR_KO missing) fall back to the
# legacy single-pass behavior against whichever emulator is already loaded.
# This keeps the script useful for hardware-attached test rigs where the
# real datadev hardware can't be hot-swapped between modes.
# ----------------------------------------------------------------------------

run_three_irq_subtests() {
   local mode_label="$1"
   run_irq_subtest "${mode_label}/cfgIrqHold=1" \
      cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgIrqHold=1      cfgDebug=1
   run_irq_subtest "${mode_label}/cfgIrqHold=100000" \
      cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgIrqHold=100000 cfgDebug=1
   run_irq_subtest "${mode_label}/polled" \
      cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgIrqDis=1      cfgDebug=1
}

# Map an emu_irq_mode value to the dmesg substring data_dev_top.c logs at
# probe time. Used by the per-mode dmesg gate.
expected_probe_line() {
   case "$1" in
      intx) printf 'using legacy INTx interrupts' ;;
      msi)  printf 'using MSI interrupts' ;;
      msix) printf 'using MSI-X interrupts' ;;
      *)    printf 'using ?' ;;
   esac
}

if [ "${GPU_ENABLED:-0}" = "1" ]; then
   # GPU CI phase: skip the IRQ-mode sweep. The GPU phase exists to exercise
   # the GPU-async path; reloading the emulator three times to flip MSI/MSI-X
   # caps adds ~3x runtime to that already-long phase without exercising any
   # GPU-specific code path. The CPU phase runs the full sweep on the same
   # emulator binary, so MSI/MSI-X coverage is preserved.
   echo "=== GPU_ENABLED=1; running legacy single-mode IRQ test (mode sweep is CPU-phase-only) ==="
   run_three_irq_subtests "current"
elif [ -f "$EMULATOR_KO" ]; then
   echo "=== Emulator detected ($EMULATOR_KO); sweeping emu_irq_mode ==="
   for IRQ_MODE in intx msi msix; do
      echo "============================================================"
      echo "  emu_irq_mode=${IRQ_MODE}"
      echo "============================================================"

      # Reload emulator with the requested mode. The stub stays loaded
      # because the emulator depends on its symbols.
      $SUDO rmmod datadev 2>/dev/null || true
      $SUDO rmmod datadev_emulator 2>/dev/null || true
      for _ in $(seq 1 15); do [ ! -e "$DEV" ] && break; sleep 0.5; done

      if ! timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" \
            $SUDO insmod "$EMULATOR_KO" emu_irq_mode="$IRQ_MODE"; then
         echo "[FAIL] could not load emulator with emu_irq_mode=$IRQ_MODE"
         FAILED=$((FAILED + 1))
         continue
      fi

      # Run the existing three subtests; they reload datadev each cycle
      # and (via run_irq_cycle) stash the post-probe dmesg snapshot in
      # CYCLE_PROBE_LINE before dmaLoopTest spam can evict it.
      FAIL_BEFORE=$FAILED
      run_three_irq_subtests "$IRQ_MODE"

      # Mode-selection assertion. Use the probe line captured during the
      # last inner subtest (dev_info renders as "datadev 0000:bb:dd.f:
      # Init: Probe: using <kind> interrupts, irq=N"; we stripped to just
      # the "Probe: using <kind> interrupts" middle phrase).
      #
      # Only run this gate when all three subtests passed. If a subtest
      # failed it has already been counted, and CYCLE_PROBE_LINE may be
      # empty/stale from an aborted cycle -- asserting on it here would
      # double-count the failure with a misleading "probe line mismatch".
      # When the subtests pass, this still catches a wrong-branch cascade
      # (dataplane OK but datadev picked the wrong interrupt kind).
      EXPECTED=$(expected_probe_line "$IRQ_MODE")
      if [ "$FAILED" -ne "$FAIL_BEFORE" ]; then
         echo "[SKIP] irq_modes ${IRQ_MODE} -- probe-line assertion skipped (a subtest already failed this mode)"
      elif [ "$CYCLE_PROBE_LINE" = "Probe: ${EXPECTED}" ]; then
         echo "[PASS] irq_modes ${IRQ_MODE} -- datadev selected: ${EXPECTED}"
      else
         echo "[FAIL] irq_modes ${IRQ_MODE} -- captured probe line mismatch:"
         echo "        expected: 'Probe: ${EXPECTED}'"
         echo "        captured: '${CYCLE_PROBE_LINE}'"
         FAILED=$((FAILED + 1))
      fi
   done
else
   echo "=== Emulator not found ($EMULATOR_KO); legacy single-mode pass ==="
   run_three_irq_subtests "current"
fi

# --- Cleanup: restore default parameters ---
$SUDO rmmod datadev 2>/dev/null || true
for _ in $(seq 1 15); do [ ! -e "$DEV" ] && break; sleep 0.5; done
timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" $SUDO insmod "$DATADEV_KO" cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgDebug=1 2>/dev/null || true
if [ ! -e "$DEV" ]; then
   DATADEV_MAJOR=$(awk '$2 == "datadev_0" { print $1 }' /proc/devices)
   $SUDO mknod "$DEV" c "$DATADEV_MAJOR" 0
   $SUDO chmod 666 "$DEV"
fi
$SUDO chmod 666 "$DEV" 2>/dev/null || true

exit "$FAILED"
