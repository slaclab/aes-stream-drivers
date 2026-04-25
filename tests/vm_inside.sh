#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    VM-inside test orchestrator. Runs inside the QEMU VM as root to load
#    modules, execute the test suite, and check dmesg for errors.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Runs INSIDE the QEMU VM (as root) once the host project directory has
# been mounted at /mnt/host via 9p/virtfs. Invoked by cloud-init runcmd
# from run_local_ci.sh.
#
# Responsibilities:
#   1. Load nvidia_p2p_stub.ko first — datadev_emulator's module_init
#      eagerly calls emu_gpu_register_drain_cb() (exported by the stub),
#      so insmod of the emulator fails with "Unknown symbol" if the stub
#      is not already live. Mirrors scripts/ci/load-modules-cpu.sh.
#   2. Load datadev_emulator.ko (built on the host, shared via 9p)
#   3. Wait for emulator init confirmation in dmesg
#   4. Load datadev.ko with cfgDebug=1 for ISR verification
#   5. Wait for /dev/datadev_0 and /proc/datadev_0
#   6. Run tests/run_tests.sh (the current CPU-phase test aggregator --
#      see run_tests.sh for the authoritative list of sub-tests)
#   7. Run tests/test_params.sh (module reload with custom parameters)
#   8. Unload modules in reverse load order
#      (datadev -> datadev_emulator -> nvidia_p2p_stub)
#   9. Check dmesg for oops/panic/BUG/WARNING (matches scripts/ci/check-dmesg.sh)
#
# Mirrors the module-load sequence from the cpu_test job in
# .github/workflows/ci_pipeline.yml so behavior is identical between the
# local VM and CI.
#
# Exit code: 0 on all-pass, non-zero on any failure.
# ----------------------------------------------------------------------------

set -uo pipefail

HOST=/mnt/host
TIMEOUT_SEC=30
EXIT_CODE=0

# Preserve the first non-zero rc so the final exit reflects the root-cause
# subtest, not whichever failing step ran last. Without this, a failure in
# run_tests.sh (line ~110) followed by a failure in test_params.sh or the
# dmesg gate would clobber the earlier rc via `|| EXIT_CODE=$?`.
record_rc() {
   [ "$EXIT_CODE" -eq 0 ] && EXIT_CODE=$1
}

# Inject a unique baseline marker so the post-run oops/panic/BUG/WARNING
# gate only scans driver-induced lines (the delta after module load), not
# boot/earlier messages that can false-positive. Mirrors
# scripts/ci/load-modules-cpu.sh and scripts/ci/check-dmesg.sh.
CI_DMESG_MARKER="BASELINE-vm-inside-$(cat /proc/sys/kernel/random/uuid)"
echo "$CI_DMESG_MARKER" > /dev/kmsg
# Verify the marker landed in dmesg. If /dev/kmsg write silently dropped
# (permissions, throttling, etc.), the awk-delta extractor below would
# return an empty DELTA and the oops/panic/BUG/WARNING gate would trivially
# pass. Fall back to a full-ring scan by clearing the marker — `index($0,"")`
# returns 1 in awk, so the empty-marker case effectively scans all dmesg
# lines (minus line 1) rather than producing a false PASS.
sleep 0.2
if ! dmesg | grep -qF "$CI_DMESG_MARKER"; then
   echo "WARN: baseline marker not found in dmesg after /dev/kmsg write — falling back to full-ring scan"
   CI_DMESG_MARKER=""
fi

# Load nvidia_p2p_stub.ko BEFORE datadev_emulator.ko. The emulator's
# module_init calls emu_gpu_register_drain_cb() (exported by the stub),
# so loading the emulator first fails with "Unknown symbol
# emu_gpu_register_drain_cb (err -2)". Mirrors the load order in
# scripts/ci/load-modules-cpu.sh.
echo "=== VM: Loading nvidia_p2p_stub module ==="
insmod "$HOST/emulator/gpu_stub/nvidia_p2p_stub.ko" || {
   echo "FAIL: insmod nvidia_p2p_stub"
   exit 1
}
timeout $TIMEOUT_SEC bash -c \
   'until [ "$(cat /sys/module/nvidia_p2p_stub/initstate 2>/dev/null)" = live ]; do sleep 0.5; done' || {
   echo "FAIL: nvidia_p2p_stub module did not initialize within ${TIMEOUT_SEC}s"
   dmesg | tail -50
   exit 1
}
echo "  nvidia_p2p_stub loaded"

echo "=== VM: Loading emulator module ==="
insmod "$HOST/emulator/driver/datadev_emulator.ko" || {
   echo "FAIL: insmod emulator"
   exit 1
}

# Poll /sys/module/.../initstate rather than `dmesg | tail -10`; the tail
# window can race and drop the success marker when module init emits a
# burst of follow-up lines. Matches scripts/ci/test-cpu.sh.
timeout $TIMEOUT_SEC bash -c \
   'until [ "$(cat /sys/module/datadev_emulator/initstate 2>/dev/null)" = live ]; do sleep 0.5; done' || {
   echo "FAIL: Emulator module did not initialize within ${TIMEOUT_SEC}s"
   dmesg | tail -50
   exit 1
}
echo "  emulator loaded"

echo "=== VM: Loading datadev driver ==="
insmod "$HOST/data_dev/driver/datadev.ko" cfgDebug=1 || {
   echo "FAIL: insmod datadev"
   exit 1
}

timeout $TIMEOUT_SEC bash -c \
   'until [ -e /dev/datadev_0 ]; do sleep 0.5; done' || {
   echo "FAIL: /dev/datadev_0 not found within ${TIMEOUT_SEC}s"
   dmesg | tail -50
   exit 1
}
echo "  /dev/datadev_0 exists"

timeout $TIMEOUT_SEC bash -c \
   'until [ -e /proc/datadev_0 ]; do sleep 0.5; done' || {
   echo "FAIL: /proc/datadev_0 not found within ${TIMEOUT_SEC}s"
   dmesg | tail -50
   exit 1
}
echo "  /proc/datadev_0 exists"

chmod 666 /dev/datadev_0
# Brief settle time so the emulator's DMA engine has captured initial RX buffers
sleep 2

echo "=== VM: Running Phase 3 test suite ==="
DEV=/dev/datadev_0 \
APP_BIN="$HOST/data_dev/app/bin" \
TESTS_DIR="$HOST/tests" \
   bash "$HOST/tests/run_tests.sh" || record_rc $?

echo "=== VM: Running module parameter validation ==="
DEV=/dev/datadev_0 \
APP_BIN="$HOST/data_dev/app/bin" \
DATADEV_KO="$HOST/data_dev/driver/datadev.ko" \
CUSTOM_TX=256 \
CUSTOM_RX=256 \
CUSTOM_SIZE=65536 \
   bash "$HOST/tests/test_params.sh" || record_rc $?

echo "=== VM: Unloading modules ==="
# Reverse load order: datadev -> datadev_emulator -> nvidia_p2p_stub.
# The stub must be unloaded LAST because the emulator still owns the
# drain callback registration until its rmmod completes.
rmmod datadev 2>/dev/null || true
sleep 1
rmmod datadev_emulator 2>/dev/null || true
sleep 1
rmmod nvidia_p2p_stub 2>/dev/null || true

echo "=== VM: Checking dmesg for errors (baseline-delta) ==="
# Extract the post-marker delta with awk+index() so any regex metacharacters
# in the UUID-based marker are matched literally. Benign kernel-cmdline
# echoes ('drm panic', 'panic=', 'panic_on_oops', 'panic_on_warn') are
# excluded to match scripts/ci/check-dmesg.sh behavior.
DELTA="$(dmesg | awk -v m="$CI_DMESG_MARKER" 'f{print} index($0,m){f=1}')"
# Use printf '%s\n' instead of echo for $DELTA: kernel-log content is
# trusted but echo's option/escape parsing is implementation-defined for
# leading -n/-e or backslash sequences, so printf is the defensive default
# for variable data being printed verbatim.
if printf '%s\n' "$DELTA" | grep -iE 'oops|panic|BUG:' | grep -viE 'drm panic|panic=|panic_on_oops|panic_on_warn'; then
   echo "FAIL: Kernel errors detected in driver-induced dmesg delta"
   record_rc 1
fi
# WARNING: gate — mirrors scripts/ci/check-dmesg.sh so a kernel WARN_ON()
# in the delta fails locally the same way it fails in CI. No benign
# exclusions here: the delta is post-load by construction, and any
# cmdline-echo 'panic_on_warn' is already filtered above.
if printf '%s\n' "$DELTA" | grep -iE 'WARNING:'; then
   echo "FAIL: Kernel warnings detected in driver-induced dmesg delta"
   record_rc 1
fi

if [ "$EXIT_CODE" -eq 0 ]; then
   echo "=== VM: ALL PASS ==="
else
   echo "=== VM: FAIL (exit=$EXIT_CODE) ==="
fi

exit $EXIT_CODE
