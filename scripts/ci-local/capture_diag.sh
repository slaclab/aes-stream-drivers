#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    Hang-diagnostic capture. Collects sysrq-t task dump, dmesg, and
#    /proc stack traces from a hung aes-ci guest via SSH and key-injection.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Captures the diagnostic triple from a running aes-ci guest that has likely
# hung inside an insmod: (1) sysrq-t kernel task dump, (2) full dmesg, (3)
# /proc/<pid>/stack + /proc/<pid>/status for every stuck insmod process.
# Additionally: /sys/module/<m>/initstate for the three aes-stream modules
# and a snapshot of the libvirt-captured serial console capture.
#
# Runs on the HOST. SSHes into the guest for the primary capture path; uses
# the qemu-monitor key-injection belt (magic sysrq) that works even if the
# guest SSHD is unresponsive.
#
# Usage:
#   scripts/ci-local/capture_diag.sh [OUTPUT_DIR]
#
#   OUTPUT_DIR defaults to .diag/<timestamp> in the current working directory.
#
# Environment variables:
#   AES_CI_CACHE_DIR  (default: $HOME/.cache/aes-ci-parity)
#   AES_CI_DOMAIN     (default: aes-ci)
#
# Output filename set matches the CI workflow's artifact collector naming
# (.github/workflows/ci_pipeline.yml), so downstream tooling needs no
# changes; see the write sites below for the authoritative list.
#
# Exit codes:
#   0 = all capture steps succeeded or produced partial output (best-effort)
#   1 = couldn't reach the guest at all (no SSH, no virsh agent, no serial)
# ----------------------------------------------------------------------------

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"

CACHE_DIR="$AES_CI_CACHE_DIR_RESOLVED"
DOM="${AES_CI_DOMAIN:-aes-ci}"
SSH_KEY="$CACHE_DIR/id_ed25519"
# Host-side libvirt serial capture path. provision_vm.sh picks the same
# NFS-aware path: AES_CI_SERIAL_LOG override, else /tmp/... on NFS homes,
# else $CACHE_DIR/${DOM}-serial.log. Mirror the same logic so the capture
# and the writer agree.
HOST_SERIAL_STEM="${DOM}-serial"
if [ -n "$AES_CI_SERIAL_LOG" ]; then
   HOST_SERIAL="$AES_CI_SERIAL_LOG"
elif [ "$(stat -f -c %T "$CACHE_DIR" 2>/dev/null)" = "nfs" ]; then
   HOST_SERIAL="/tmp/aes-ci-${DOM}-serial.log"
else
   HOST_SERIAL="$CACHE_DIR/${HOST_SERIAL_STEM}.log"
fi

OUTPUT_DIR="${1:-.diag/$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUTPUT_DIR"

echo_header "Capturing hang diagnostics to $OUTPUT_DIR"

# ----------------------------------------------------------------------------
# Guest IP discovery — via qemu-guest-agent (never scrape DHCP leases).
# Small retry loop because the agent occasionally lags the kernel's DHCP
# lease by a couple seconds. Interface filter enp*/eth* skips lo and docker0.
# ----------------------------------------------------------------------------
GUEST_IP=""
for _ in 1 2 3 4 5; do
   GUEST_IP=$(virsh domifaddr --source agent "$DOM" 2>/dev/null | awk '/^ enp[0-9]|^ eth[0-9]/ && /ipv4/ {print $4}' | cut -d/ -f1 | head -1)
   if [ -n "$GUEST_IP" ]; then break; fi
   sleep 2
done

if [ -z "$GUEST_IP" ]; then
   echo_warn "Could not reach qemu-guest-agent — guest may be completely wedged."
   echo_warn "  Falling back to the key-injection belt + host-side serial snapshot only."
   GUEST_IP=""
fi

SSH_OPTS="-i $SSH_KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5"
SSH_CMD="ssh $SSH_OPTS ubuntu@$GUEST_IP"

# ----------------------------------------------------------------------------
# Belt: host-side magic-sysrq via key-injection. Dumps every task's stack
# into the guest kernel ring buffer, which is then written to the libvirt
# serial capture file because provision_vm.sh configures --serial file,path=...
# This path works even when guest SSHD is hung (the key-event goes via QEMU
# monitor, not userspace).
# ----------------------------------------------------------------------------
echo_step "Triggering magic sysrq-t"
virsh send-key "$DOM" KEY_LEFTALT KEY_SYSRQ KEY_T 2>&1 | sed 's/^/  /' || {
   echo_warn "  key-injection failed — domain may be paused or undefined"
}
# Let the kernel finish writing the task dump to the ring buffer before
# snapshotting the serial capture.
sleep 3

# ----------------------------------------------------------------------------
# Host-side serial snapshot
# ----------------------------------------------------------------------------
echo_step "Copying libvirt-captured serial stream"
OUTPUT_SERIAL="$OUTPUT_DIR/serial.log"
if [ -f "$HOST_SERIAL" ]; then
   cp "$HOST_SERIAL" "$OUTPUT_SERIAL"
   echo_info "  captured $(wc -l < "$OUTPUT_SERIAL") lines"
else
   echo_warn "  $HOST_SERIAL not found — did provision_vm.sh create the domain?"
fi

# ----------------------------------------------------------------------------
# Suspenders: SSH-driven guest-side captures. `set +e` around the block so
# one wedged probe does not abort the remaining captures.
# ----------------------------------------------------------------------------
if [ -n "$GUEST_IP" ]; then
   echo_step "Capturing guest-side diagnostics via SSH"

   set +e

   # Full dmesg + convenience tail (dmesg-tail alias file: last 500 lines)
   $SSH_CMD 'sudo dmesg' > "$OUTPUT_DIR/dmesg.log" 2>&1
   $SSH_CMD 'sudo dmesg | tail -500' > "$OUTPUT_DIR/dmesg-tail.log" 2>&1

   # /proc/<pid>/stack + status for every stuck insmod pid.
   # `pgrep -f` matches the insmod command line produced by
   # scripts/ci/load-modules-cpu.sh.  Kept on one logical line so no `sudo`
   # call leads a source line on the HOST (sudo only executes inside the
   # SSH-quoted string on the guest).
   $SSH_CMD 'for p in $(pgrep -f "insmod.*datadev"); do echo "=== pid $p ==="; { sudo cat /proc/$p/stack 2>/dev/null || echo "(no /proc/$p/stack)"; }; echo "---"; { sudo cat /proc/$p/status 2>/dev/null || echo "(no /proc/$p/status)"; }; echo ""; done' > "$OUTPUT_DIR/proc-stacks.log" 2>&1

   # Guest-side sysrq-t trigger + dmesg tail capture. Complements the belt
   # above: where the belt dumps into the host serial capture, this dumps
   # what the guest kernel printed into its own ring buffer after the
   # trigger fires.
   $SSH_CMD 'echo t | sudo tee /proc/sysrq-trigger > /dev/null && sleep 2 && sudo dmesg | tail -500' \
      > "$OUTPUT_DIR/sysrq-t-ringbuf.log" 2>&1

   # Module state — which of the three are loaded, which in what initstate?
   $SSH_CMD 'for m in datadev datadev_emulator nvidia_p2p_stub; do
                echo "=== $m ==="
                cat /sys/module/$m/initstate 2>/dev/null || echo "(not loaded)"
             done' > "$OUTPUT_DIR/module-state.log" 2>&1

   set -e
else
   echo_warn "  Skipping SSH-driven captures (no guest agent response)"
fi

# ----------------------------------------------------------------------------
# Summary
# ----------------------------------------------------------------------------
echo_header "Capture complete"
echo_info "Diagnostic files:"
ls -la "$OUTPUT_DIR" 2>/dev/null | sed 's/^/  /'
echo_info ""
echo_info "Artifact layout matches the CI workflow's artifact collector —"
echo_info "it will recognize this filename set without modification."

# Exit 0 if we got ANY output; exit 1 if truly nothing could be collected.
if [ -z "$(ls -A "$OUTPUT_DIR" 2>/dev/null)" ]; then
   echo_fail "No diagnostic files captured — guest and host both unreachable"
   exit 1
fi
exit 0
