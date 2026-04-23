#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    VM-inside test orchestrator (GPU-enabled stack). Runs inside the QEMU VM
#    to load GPU modules, execute Phase 3+4 tests, and check dmesg for errors.
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
# from run_local_ci.sh when ENABLE_GPU=1 is exported.
#
# Parallel to tests/vm_inside.sh but inserts nvidia_p2p_stub between the
# emulator and datadev, and exports GPU_ENABLED=1 so run_tests.sh runs
# the Phase 4 GPU ioctl + /proc tests in addition to the Phase 3 suite.
#
# Responsibilities:
#   1. Load datadev_emulator.ko (built on the host)
#   2. Load nvidia_p2p_stub.ko (built on the host)  -- NEW vs vm_inside.sh
#   3. Load datadev.ko (GPU build: built with NVIDIA_DRIVERS=$HOST/emulator/gpu_stub)
#   4. Wait for /dev/datadev_0 and /proc/datadev_0
#   5. Confirm "Configured for GpuAsyncCore version 4" in dmesg
#   6. Run tests/run_tests.sh with GPU_ENABLED=1 (Phase 3 + Phase 4 cases)
#   7. Unload in reverse order: datadev -> nvidia_p2p_stub -> datadev_emulator
#   8. Check dmesg for oops/panic/BUG
#
# Mirrors the module-load sequence from the build_and_test_gpu CI job so
# behavior is identical between local VM and CI.
#
# Exit code: 0 on all-pass, non-zero on any failure.
# ----------------------------------------------------------------------------

set -uo pipefail

HOST=/mnt/host
TIMEOUT_SEC=30
EXIT_CODE=0

echo "=== VM-GPU: Loading emulator module ==="
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

if ! dmesg | grep -q "BAR0 GPU Async V4 initialized"; then
   echo "FAIL: GPU Async V4 init log line not found"
   dmesg | tail -50
   exit 1
fi
echo "  GPU Async V4 registers initialized"

echo "=== VM-GPU: Loading nvidia_p2p_stub ==="
insmod "$HOST/emulator/gpu_stub/nvidia_p2p_stub.ko" || {
   echo "FAIL: insmod nvidia_p2p_stub (was it built? 'make -C emulator/gpu_stub' on host)"
   exit 1
}
echo "  nvidia_p2p_stub loaded"

echo "=== VM-GPU: Loading datadev (GPU build) ==="
insmod "$HOST/data_dev/driver/datadev.ko" cfgDebug=1 || {
   echo "FAIL: insmod datadev (was it built with NVIDIA_DRIVERS=\$HOST/emulator/gpu_stub?)"
   dmesg | tail -30
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

if ! dmesg | grep -q "Configured for GpuAsyncCore version 4"; then
   echo "FAIL: Gpu_Init V4 confirmation not found in dmesg"
   dmesg | tail -100
   EXIT_CODE=1
else
   echo "  Gpu_Init reported GpuAsyncCore version 4"
fi

chmod 666 /dev/datadev_0
sleep 2

echo "=== VM-GPU: Running Phase 3 + Phase 4 test suite ==="
DEV=/dev/datadev_0 \
APP_BIN="$HOST/data_dev/app/bin" \
TESTS_DIR="$HOST/tests" \
GPU_ENABLED=1 \
   bash "$HOST/tests/run_tests.sh" || EXIT_CODE=$?

echo "=== VM-GPU: Running module parameter validation ==="
DEV=/dev/datadev_0 \
APP_BIN="$HOST/data_dev/app/bin" \
DATADEV_KO="$HOST/data_dev/driver/datadev.ko" \
CUSTOM_TX=256 \
CUSTOM_RX=256 \
CUSTOM_SIZE=65536 \
   bash "$HOST/tests/test_params.sh" || EXIT_CODE=$?

echo "=== VM-GPU: Unloading modules ==="
rmmod datadev 2>/dev/null || true
sleep 1
rmmod nvidia_p2p_stub 2>/dev/null || true
rmmod datadev_emulator 2>/dev/null || true

echo "=== VM-GPU: Checking dmesg for errors ==="
if dmesg | grep -iE 'oops|panic|BUG:'; then
   echo "FAIL: Kernel errors detected in dmesg"
   EXIT_CODE=1
fi

if [ "$EXIT_CODE" -eq 0 ]; then
   echo "=== VM-GPU: ALL PASS ==="
else
   echo "=== VM-GPU: FAIL (exit=$EXIT_CODE) ==="
fi

exit $EXIT_CODE
