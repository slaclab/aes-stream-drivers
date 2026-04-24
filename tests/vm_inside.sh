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
#   1. Load datadev_emulator.ko (built on the host, shared via 9p)
#   2. Wait for emulator init confirmation in dmesg
#   3. Load datadev.ko with cfgDebug=1 for ISR verification
#   4. Wait for /dev/datadev_0 and /proc/datadev_0
#   5. Run tests/run_tests.sh (ioctl, file-ops, error, multichannel, proc, rate)
#   6. Run tests/test_params.sh (module reload with custom parameters)
#   7. Unload modules in reverse order
#   8. Check dmesg for oops/panic/BUG
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

echo "=== VM: Loading emulator module ==="
insmod "$HOST/emulator/driver/datadev_emulator.ko" || {
   echo "FAIL: insmod emulator"
   exit 1
}

timeout $TIMEOUT_SEC bash -c \
   'until dmesg | tail -10 | grep -q "emulator loaded successfully"; do sleep 0.5; done' || {
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
   bash "$HOST/tests/run_tests.sh" || EXIT_CODE=$?

echo "=== VM: Running module parameter validation ==="
DEV=/dev/datadev_0 \
APP_BIN="$HOST/data_dev/app/bin" \
DATADEV_KO="$HOST/data_dev/driver/datadev.ko" \
CUSTOM_TX=256 \
CUSTOM_RX=256 \
CUSTOM_SIZE=65536 \
   bash "$HOST/tests/test_params.sh" || EXIT_CODE=$?

echo "=== VM: Unloading modules ==="
rmmod datadev 2>/dev/null || true
sleep 1
rmmod datadev_emulator 2>/dev/null || true

echo "=== VM: Checking dmesg for errors ==="
if dmesg | grep -iE 'oops|panic|BUG:'; then
   echo "FAIL: Kernel errors detected in dmesg"
   EXIT_CODE=1
fi

if [ "$EXIT_CODE" -eq 0 ]; then
   echo "=== VM: ALL PASS ==="
else
   echo "=== VM: FAIL (exit=$EXIT_CODE) ==="
fi

exit $EXIT_CODE
