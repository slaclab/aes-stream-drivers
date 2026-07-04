#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    DMA address-width clamp + reachability-guard test. Exercises the hardening
#    that keeps the datadev CPU path within the AxiStreamDmaV2 40-bit descriptor
#    limit (surf AxiStreamDmaV2Desc.vhd asserts ADDR_WIDTH_C <= 40).
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Two subtests, both run against the emulator (which advertises a 64-bit AXI
# width in channelCount[15:8] and sets the 128-bit-descriptor enable bit):
#
#   A) Clamp: load datadev normally. The driver must clamp the FW-reported
#      64-bit width down to the 40-bit descriptor limit -- verified by the
#      "Init: Using 40-bit DMA mask." log line -- and the device must still
#      come up (/proc/datadev_0 present).
#
#   B) Guard: reload datadev with cfgAddrTestWidth below the buffer size so
#      dmaAllocBuffers()'s reachability guard rejects every buffer. The probe
#      must fail cleanly: the guard "exceeds DMA mask" dev_err appears, the
#      device does NOT bind (/proc/datadev_0 absent), and no oops/panic/BUG/
#      WARNING is emitted.
#
# dmesg is scoped per subtest with a unique /dev/kmsg marker rather than a
# line-count offset: under the emulator's logging the kernel ring buffer runs
# at capacity, so a "dmesg | wc -l" delta silently reads past the end. The
# marker survives ring pressure because it is emitted immediately before insmod
# and read back within ~1s (and cfgDebug=0 keeps datadev quiet). This mirrors
# the marker pattern in scripts/ci/load-modules-cpu.sh / check-dmesg.sh.
#
# Requires root/sudo (reloads kernel modules) and the emulator already loaded.
# Skips gracefully when either is unavailable so it can be dry-run on an
# unprivileged host. Mirrors the reload/skip conventions of test_params.sh.
#
# Environment variables:
#   DEV             Device path (default: /dev/datadev_0)
#   DATADEV_KO      Path to datadev.ko (default: data_dev/driver/datadev.ko)
#   TEST_SIZE       cfgSize used for both loads (default: 65536)
#   TEST_WIDTH      cfgAddrTestWidth for subtest B (default: 12 -> 4096-byte
#                   ceiling, always below TEST_SIZE so the guard trips)
#   EXPECT_CLAMP_BITS  Expected clamped mask width (default: 40)
# ----------------------------------------------------------------------------

set -uo pipefail

DEV="${DEV:-/dev/datadev_0}"
DATADEV_KO="${DATADEV_KO:-data_dev/driver/datadev.ko}"
INSMOD_TIMEOUT_SEC="${INSMOD_TIMEOUT_SEC:-120}"
TIMEOUT_SEC="${TIMEOUT_SEC:-15}"
TEST_SIZE="${TEST_SIZE:-65536}"
TEST_WIDTH="${TEST_WIDTH:-12}"
EXPECT_CLAMP_BITS="${EXPECT_CLAMP_BITS:-40}"
PROC="/proc/datadev_0"

echo "=== DMA address-width clamp + guard test ==="
echo "DATADEV_KO=$DATADEV_KO TEST_SIZE=$TEST_SIZE TEST_WIDTH=$TEST_WIDTH"

# Require root/sudo capability (this test reloads kernel modules). Skip
# gracefully if neither is available so unprivileged dry-runs do not fail CI.
if [ "$(id -u)" -ne 0 ] && ! sudo -n true 2>/dev/null; then
   echo "SKIP: test_addr_width.sh requires root/sudo (no TTY sudo allowed)"
   exit 0
fi
if [ "$(id -u)" -eq 0 ]; then SUDO=""; else SUDO="sudo"; fi

if [ ! -f "$DATADEV_KO" ]; then
   echo "SKIP: module file not found: $DATADEV_KO"
   exit 0
fi

# The emulator supplies the virtual PCI device (and the 64-bit width report).
if [ ! -e /sys/module/datadev_emulator ]; then
   echo "SKIP: datadev_emulator not loaded (required to provide the virtual device)"
   exit 0
fi

ERRORS=0
MARK=""

# Reload datadev with the supplied insmod parameters, scoping dmesg to this
# load via a fresh /dev/kmsg marker (stored in $MARK). Waits for the previous
# instance to fully unload via /sys/module/datadev (reliable; /dev and /proc
# nodes can linger as stale mknod entries in a udev-less container). insmod
# returns 0 even when a device fails to bind (pci_register_driver succeeds
# regardless), so callers inspect /proc and the marker-scoped dmesg instead.
reload_datadev() {
   $SUDO rmmod datadev 2>/dev/null || true
   for _ in $(seq 1 30); do [ ! -e /sys/module/datadev ] && break; sleep 0.5; done
   MARK="ADDRW-MARK-$$-${RANDOM}-${1:-x}"
   echo "$MARK" | $SUDO tee /dev/kmsg >/dev/null 2>&1 || true
   shift 2>/dev/null || true
   timeout --kill-after=5s "${INSMOD_TIMEOUT_SEC}s" \
      $SUDO insmod "$DATADEV_KO" "$@" 2>/dev/null || true
   sleep 1
}

# Emit the current ring contents after the marker emitted by reload_datadev.
dmesg_since() { $SUDO dmesg | awk -v m="$MARK" 'p {print} index($0, m) {p=1}'; }

# ---------------------------------------------------------------------------
# Subtest A: FW-reported 64-bit width is clamped to the descriptor limit.
# ---------------------------------------------------------------------------
echo "--- Subtest A: clamp FW 64-bit width to ${EXPECT_CLAMP_BITS}-bit ---"
reload_datadev A cfgDebug=0 cfgTxCount=64 cfgRxCount=64 cfgSize="$TEST_SIZE" cfgMode=1
DELTA_A=$(dmesg_since)

if printf '%s\n' "$DELTA_A" | grep -qE "Using ${EXPECT_CLAMP_BITS}-bit DMA mask"; then
   echo "PASS: driver clamped to ${EXPECT_CLAMP_BITS}-bit DMA mask"
else
   echo "FAIL: expected 'Using ${EXPECT_CLAMP_BITS}-bit DMA mask' in dmesg"
   printf '%s\n' "$DELTA_A" | grep -iE "DMA mask|AXI width|clamp" || true
   ERRORS=$((ERRORS + 1))
fi

# Device must bind: Dma_Init creates the proc entry on a successful probe.
for _ in $(seq 1 "$TIMEOUT_SEC"); do [ -e "$PROC" ] && break; sleep 0.5; done
if [ -e "$PROC" ]; then
   echo "PASS: device bound after clamped load ($PROC present)"
else
   echo "FAIL: device did not bind after normal (clamped) load"
   ERRORS=$((ERRORS + 1))
fi

# ---------------------------------------------------------------------------
# Subtest B: reachability guard rejects unreachable buffers, probe fails clean.
# ---------------------------------------------------------------------------
echo "--- Subtest B: guard rejects buffers (cfgAddrTestWidth=${TEST_WIDTH}) ---"
reload_datadev B cfgDebug=0 cfgTxCount=64 cfgRxCount=64 cfgSize="$TEST_SIZE" \
               cfgMode=1 cfgAddrTestWidth="$TEST_WIDTH"
DELTA_B=$(dmesg_since)

if printf '%s\n' "$DELTA_B" | grep -qE "exceeds DMA mask"; then
   echo "PASS: reachability guard fired (dev_err 'exceeds DMA mask')"
else
   echo "FAIL: guard dev_err 'exceeds DMA mask' not found in dmesg"
   ERRORS=$((ERRORS + 1))
fi

# Probe must have failed: Dma_Init removes the proc entry on the guard path.
if [ -e "$PROC" ]; then
   echo "FAIL: device bound despite guard rejection ($PROC present)"
   ERRORS=$((ERRORS + 1))
else
   echo "PASS: device did not bind (probe failed cleanly, $PROC absent)"
fi

# The guard uses dev_err, not WARN, and rejects before the descriptor write, so
# no oops/panic/BUG/WARNING must appear (BENIGN kernel-cmdline echoes excluded,
# matching scripts/ci/check-dmesg.sh).
if printf '%s\n' "$DELTA_B" | grep -iE 'oops|panic|BUG:|WARNING:' \
      | grep -viE 'drm panic|panic=|panic_on_oops|panic_on_warn'; then
   echo "FAIL: kernel oops/panic/BUG/WARNING during guard rejection"
   ERRORS=$((ERRORS + 1))
else
   echo "PASS: no kernel oops/panic/BUG/WARNING during guard rejection"
fi

# ---------------------------------------------------------------------------
# Cleanup: restore a normal datadev load so downstream tests find the device.
# ---------------------------------------------------------------------------
echo "Restoring default datadev configuration"
reload_datadev restore cfgDebug=1 cfgTxCount=64 cfgRxCount=64 cfgSize="$TEST_SIZE" cfgMode=1
for _ in $(seq 1 "$TIMEOUT_SEC"); do [ -e "$PROC" ] && break; sleep 0.5; done
# Recreate the /dev node when udev is absent (Docker), mirroring the CI scripts.
if [ ! -e "$DEV" ] && [ -e /proc/devices ]; then
   MAJ=$(awk '$2 == "datadev_0" { print $1 }' /proc/devices)
   if [ -n "$MAJ" ]; then
      $SUDO mknod "$DEV" c "$MAJ" 0
      $SUDO chmod 666 "$DEV"
   fi
fi
[ -e "$DEV" ] && $SUDO chmod 666 "$DEV" 2>/dev/null || true

echo "=== addr_width test: $ERRORS errors ==="
exit "$ERRORS"
