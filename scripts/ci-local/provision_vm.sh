#!/bin/bash
# -----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# -----------------------------------------------------------------------------
# Description:
#    aes-ci parity VM provisioner. Idempotent libvirt/QEMU+KVM VM lifecycle
#    with cloud-init, Azure kernel verification, and Docker 28.x checks.
# -----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to the
# license terms in the LICENSE.txt file found in the top-level directory of
# this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# -----------------------------------------------------------------------------
# Idempotent libvirt/QEMU+KVM VM lifecycle for the aes-ci parity harness.
# Ten stages:
#   1. base cloud image download (one-time)
#   2. SSH keypair generation (per-host, per-user; persistent)
#   3. user-data rendering (substitute @@SSH_PUBKEY@@ sentinel with pubkey)
#   4. idempotency guard (hash-based marker + libvirt dominfo check)
#   5. overlay + seed ISO creation
#   6. teardown of any stale domain
#   7. virt-install --import
#   8. wait for /var/lib/aes-ci-cloud-init-done marker in guest (20-min budget)
#   9. verify uname -r contains 'azure' and docker server is 28.x+
#  10. write the provisioning marker
#
# Flags:
#   --reset  destroy+undefine+wipe overlay+marker, then provision from scratch
#   --clean  like --reset but exit after teardown (no re-provision)
#
# Environment variables (all optional):
#   AES_CI_CACHE_DIR       (default: $HOME/.cache/aes-ci-parity)
#   AES_CI_BASE_IMAGE_URL  (default: Canonical noble qcow2)
#   AES_CI_VM_MEMORY       (default: 4096 MB)
#   AES_CI_VM_VCPUS        (default: 4)
#   AES_CI_DOMAIN          (default: aes-ci)
#   AES_CI_REPO_SOURCE     (default: git rev-parse --show-toplevel, falls
#                           back to $PWD outside a git tree) — host path
#                           shared into the guest via virtiofs at
#                           /mnt/aes-stream-drivers. Override to a local-
#                           disk path on SLAC S3DF / NFS root_squash hosts;
#                           libvirt-qemu (uid 64055) must have read access
#                           to this path.
#   AES_CI_SERIAL_LOG      (default: /tmp/aes-ci-${DOM}-serial.log when
#                           $CACHE_DIR is on NFS, else $CACHE_DIR/${DOM}-serial.log)
#                           libvirt daemon opens this path as root; NFS
#                           root_squash (e.g. SLAC S3DF) maps root→nobody and
#                           denies access to any file under $HOME, so the
#                           default falls back to local disk when NFS is
#                           detected. Override explicitly for custom setups.
#
# Exit codes:
#   0 = provisioned + verified, OR idempotent skip
#   1 = kernel verification failed (no 'azure' in uname -r)
#   2 = docker verification failed (no 28.x+)
#   3 = virt-install or libvirt failed
#   4 = cloud-init did not complete within budget
#   5 = unsupported flag / usage error
# ----------------------------------------------------------------------------

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"

# ----------------------------------------------------------------------------
# Config (environment variable contract documented in scripts/ci-local/README.md)
# ----------------------------------------------------------------------------
# CACHE_DIR is resolved by lib/common.sh (NFS-aware — see AES_CI_CACHE_DIR_RESOLVED).
CACHE_DIR="$AES_CI_CACHE_DIR_RESOLVED"
BASE_URL="${AES_CI_BASE_IMAGE_URL:-https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img}"
VM_MEMORY="${AES_CI_VM_MEMORY:-4096}"
VM_VCPUS="${AES_CI_VM_VCPUS:-4}"
DOM="${AES_CI_DOMAIN:-aes-ci}"
REPO_HOST_PATH="${AES_CI_REPO_SOURCE:-$(git -C "$SCRIPT_DIR/../.." rev-parse --show-toplevel 2>/dev/null || pwd)}"

BASE="$CACHE_DIR/base-noble.qcow2"
OVERLAY="$CACHE_DIR/${DOM}.qcow2"
SEED="$CACHE_DIR/${DOM}-seed.iso"
MARKER_PREFIX="$CACHE_DIR/.provisioned-${DOM}"
SSH_KEY="$CACHE_DIR/id_ed25519"

# Serial log is opened by libvirtd (runs as root). On NFS root_squash hosts
# (SLAC S3DF, many enterprise setups) root→nobody cannot read $HOME, so file
# under $CACHE_DIR is inaccessible to the daemon. Detect NFS and fall back to
# local disk unless the user explicitly overrode via AES_CI_SERIAL_LOG.
if [ -n "$AES_CI_SERIAL_LOG" ]; then
   SERIAL_LOG="$AES_CI_SERIAL_LOG"
elif [ "$(stat -f -c %T "$CACHE_DIR" 2>/dev/null)" = "nfs" ]; then
   SERIAL_LOG="/tmp/aes-ci-${DOM}-serial.log"
else
   SERIAL_LOG="$CACHE_DIR/${DOM}-serial.log"
fi

# 20-minute cloud-init wait budget: 120 polls * 10s = 1200s.
CLOUD_INIT_POLLS=120
CLOUD_INIT_POLL_INTERVAL=10

# ----------------------------------------------------------------------------
# Flag parsing — while/case because we only accept two long-form flags.
# ----------------------------------------------------------------------------
DO_RESET=0
DO_CLEAN_EXIT=0
while [ $# -gt 0 ]; do
   case "$1" in
      --reset)
         DO_RESET=1
         ;;
      --clean)
         DO_RESET=1
         DO_CLEAN_EXIT=1
         ;;
      -h|--help)
         sed -n '2,/^# ---/p' "$0" | sed 's/^# \?//'
         exit 0
         ;;
      *)
         echo_fail "Unknown flag: $1"
         echo_fail "  Usage: $0 [--reset|--clean]"
         exit 5
         ;;
   esac
   shift
done

mkdir -p "$CACHE_DIR"
echo_header "Provisioning aes-ci parity VM (domain=$DOM)"

# ----------------------------------------------------------------------------
# Teardown helper — invoked by --reset, --clean, and the hash-mismatch path in
# Stage 4. Keeps $BASE (expensive to re-download) and $SSH_KEY (stable identity
# per host); only wipes domain-scoped state.
# ----------------------------------------------------------------------------
teardown_domain() {
   echo_step "Tearing down existing $DOM domain (if any)"
   virsh destroy  "$DOM" 2>/dev/null || true
   virsh undefine "$DOM" --remove-all-storage --snapshots-metadata --nvram 2>/dev/null || true
   rm -f "$OVERLAY" "$SEED" "$SERIAL_LOG"
   # Keep $BASE (content-addressable, expensive to re-download).
   # Keep $SSH_KEY (stable identity per host).
   rm -f "${MARKER_PREFIX}"-*
}

if [ "$DO_RESET" -eq 1 ]; then
   teardown_domain
   if [ "$DO_CLEAN_EXIT" -eq 1 ]; then
      echo_step "Teardown complete — exiting (--clean)"
      exit 0
   fi
fi

# ----------------------------------------------------------------------------
# Stage 1 — base cloud image (one-time download)
# ----------------------------------------------------------------------------
# Download to ".part" first, then atomic-rename on success. Survives Ctrl-C
# half-downloads without leaving a corrupt file at the final path.
echo_header "Stage 1 / 10: base cloud image"
if [ ! -f "$BASE" ]; then
   echo_step "Downloading Ubuntu 24.04 Noble cloud image from $BASE_URL"
   if command -v wget >/dev/null 2>&1; then
      wget -O "$BASE.part" "$BASE_URL"
   elif command -v curl >/dev/null 2>&1; then
      curl -L -o "$BASE.part" "$BASE_URL"
   else
      echo_fail "Neither wget nor curl available (preflight should have caught this)"
      exit 3
   fi
   mv "$BASE.part" "$BASE"
   echo_step "Downloaded $(du -h "$BASE" | cut -f1) to $BASE"
else
   echo_info "Base image cached: $BASE ($(du -h "$BASE" | cut -f1))"
fi

# ----------------------------------------------------------------------------
# Stage 2 — SSH keypair (per-host, per-user, persistent)
# ----------------------------------------------------------------------------
# Key lives under $CACHE_DIR and is stable across --reset. The public half is
# substituted into user-data.yaml; the private half never leaves the host.
echo_header "Stage 2 / 10: SSH keypair"
if [ ! -f "$SSH_KEY" ]; then
   echo_step "Generating ed25519 keypair at $SSH_KEY"
   ssh-keygen -t ed25519 -N '' -f "$SSH_KEY" -C "aes-ci-parity@$(hostname)"
   chmod 600 "$SSH_KEY"
else
   echo_info "SSH keypair exists: $SSH_KEY"
fi
PUBKEY="$(cat "${SSH_KEY}.pub")"

# ----------------------------------------------------------------------------
# Stage 3 — render cloud-init user-data (substitute @@SSH_PUBKEY@@ sentinel)
# ----------------------------------------------------------------------------
# The committed YAML is never mutated in-place. We render a per-run copy
# into $CACHE_DIR with the pubkey substituted, then feed that to
# cloud-localds. Pipe-delimiter because SSH keys contain '/' but never '|'.
echo_header "Stage 3 / 10: render cloud-init user-data"
echo_step "Substituting @@SSH_PUBKEY@@ into user-data template"
RENDERED_USER_DATA="$CACHE_DIR/${DOM}-user-data"
RENDERED_META_DATA="$CACHE_DIR/${DOM}-meta-data"

sed "s|@@SSH_PUBKEY@@|$PUBKEY|" \
   "$SCRIPT_DIR/cloud-init/user-data.yaml" > "$RENDERED_USER_DATA"
sed "s|local-hostname: aes-ci|local-hostname: ${DOM}|" \
   "$SCRIPT_DIR/cloud-init/meta-data.yaml" > "$RENDERED_META_DATA"

# Sanity: substitution must have worked exactly once; leftover sentinel is a bug.
if grep -q '@@SSH_PUBKEY@@' "$RENDERED_USER_DATA"; then
   echo_fail "SSH pubkey substitution failed — @@SSH_PUBKEY@@ still present"
   exit 3
fi

# ----------------------------------------------------------------------------
# Stage 4 — idempotency guard
# ----------------------------------------------------------------------------
# Marker file name encodes sha256 of rendered user-data. Match + libvirt
# dominfo success => idempotent skip. Hash mismatch => teardown + reprovision
# (user-data.yaml was edited).
echo_header "Stage 4 / 10: idempotency guard"
USER_DATA_HASH="$(sha256sum "$RENDERED_USER_DATA" | cut -d' ' -f1)"
MARKER_FILE="${MARKER_PREFIX}-${USER_DATA_HASH}"

if [ -f "$MARKER_FILE" ] && virsh dominfo "$DOM" >/dev/null 2>&1; then
   echo_step "VM already provisioned for user-data hash ${USER_DATA_HASH:0:12}"
   # Start the domain if it's defined but not running.
   STATE="$(virsh dominfo "$DOM" | awk '/^State:/ {print $2}')"
   if [ "$STATE" != "running" ]; then
      echo_step "Starting domain (was $STATE)"
      virsh start "$DOM"
   fi
   echo_step "Idempotent skip — VM ready"
   exit 0
fi

# If we reached here: either no marker, wrong-hash marker, or domain gone.
# Teardown to get to a known-clean state.
if ls "${MARKER_PREFIX}"-* >/dev/null 2>&1; then
   echo_warn "user-data hash changed — tearing down stale domain"
fi
teardown_domain

# ----------------------------------------------------------------------------
# Stage 5 — qcow2 overlay + cloud-init seed ISO
# ----------------------------------------------------------------------------
# Overlay: 40G virtual (sparse); base image is never mutated. -F qcow2 avoids
# libvirt's "unspecified backing file format" warning. Seed ISO: NoCloud
# datasource picked up by cloud-init during first boot. Fallback chain:
# cloud-localds -> genisoimage -> mkisofs.
echo_header "Stage 5 / 10: qcow2 overlay + cloud-init seed ISO"
echo_step "Creating overlay $OVERLAY (40G, backed by base)"
qemu-img create -f qcow2 -F qcow2 -b "$BASE" "$OVERLAY" 40G >/dev/null

echo_step "Generating seed ISO via cloud-localds"
if command -v cloud-localds >/dev/null 2>&1; then
   cloud-localds "$SEED" "$RENDERED_USER_DATA" "$RENDERED_META_DATA"
elif command -v genisoimage >/dev/null 2>&1; then
   genisoimage -output "$SEED" -volid cidata -joliet -rock \
      "$RENDERED_USER_DATA" "$RENDERED_META_DATA" >/dev/null 2>&1
elif command -v mkisofs >/dev/null 2>&1; then
   mkisofs -output "$SEED" -volid cidata -joliet -rock \
      "$RENDERED_USER_DATA" "$RENDERED_META_DATA" >/dev/null 2>&1
else
   echo_fail "No seed-ISO tool available (preflight should have caught this)"
   exit 3
fi

# ----------------------------------------------------------------------------
# Stage 6 — stale-domain teardown
# ----------------------------------------------------------------------------
# Handled by the hash-mismatch path in Stage 4 (teardown_domain call above).
# This banner keeps the 1-of-10 pacing honest for operators watching the log.
echo_header "Stage 6 / 10: stale-domain teardown (handled in Stage 4)"

# ----------------------------------------------------------------------------
# Stage 7 — virt-install --import
# ----------------------------------------------------------------------------
# Flag rationale:
#   import flag  : treat overlay as pre-installed disk, NOT installer boot;
#                  omitting it causes libvirt to hang at a nonexistent GRUB
#                  installer.
#   osinfo flag  : tells libvirt the guest flavor for default XML settings.
#   noautocons.  : return control to the shell after boot (don't attach console).
#   graphics none: no VNC/SPICE; we use serial for diagnostics.
#   serial file  : capture guest serial console to a host file.
#   channel unix : enable qemu-guest-agent for IP discovery via
#                  'virsh domifaddr --source agent' and guest-exec polling.
#   connect URI  : system-scoped libvirt URI — session URI cannot attach
#                  the default NAT network.
#   filesystem   : virtiofs share of the host repo checkout into the guest
#                  (tag 'aes-stream-drivers' -> /mnt/aes-stream-drivers per
#                  user-data.yaml fstab entry). Required for repo bind-mount
#                  into matrix cells; memorybacking memfd+shared is a
#                  vhost-user prerequisite for virtiofs.
echo_header "Stage 7 / 10: virt-install --import"
echo_step "Registering domain $DOM with libvirt (system URI, $VM_MEMORY MB, $VM_VCPUS vCPU)"
echo_info "Virtiofs source (host): $REPO_HOST_PATH -> guest tag 'aes-stream-drivers'"

virt-install \
   --connect qemu:///system \
   --name "$DOM" \
   --osinfo ubuntu24.04 \
   --memory "$VM_MEMORY" \
   --vcpus "$VM_VCPUS" \
   --import \
   --disk path="$OVERLAY",format=qcow2,bus=virtio \
   --disk path="$SEED",device=cdrom \
   --network network=default,model=virtio \
   --graphics none \
   --console pty,target_type=virtio \
   --serial "file,path=$SERIAL_LOG" \
   --channel "unix,target_type=virtio,name=org.qemu.guest_agent.0" \
   --filesystem "source.dir=$REPO_HOST_PATH,target.dir=aes-stream-drivers,type=mount,accessmode=passthrough,driver.type=virtiofs" \
   --memorybacking "source.type=memfd,access.mode=shared" \
   --noautoconsole \
   || {
      echo_fail "virt-install failed — see output above"
      exit 3
   }

# ----------------------------------------------------------------------------
# Stage 8 — wait for cloud-init completion marker in guest
# ----------------------------------------------------------------------------
# /var/lib/aes-ci-cloud-init-done is touched by the last runcmd in
# user-data.yaml, AFTER linux-image-azure install + reboot into the Azure
# kernel + docker install. 20-min budget (120 polls * 10s = 1200s).
#
# Poll via qemu-guest-agent guest-exec running `test -f`. capture-output:true
# gives us a JSON blob including `"exitcode":0` which we grep for; this
# prevents the false-positive "agent reachable = done" trap during the reboot
# window between first and second boot.
echo_header "Stage 8 / 10: wait for cloud-init to complete in guest"
echo_step "Polling for /var/lib/aes-ci-cloud-init-done (budget: $((CLOUD_INIT_POLLS * CLOUD_INIT_POLL_INTERVAL))s)"
echo_info "This includes: first-boot packages, Azure kernel install, reboot, Docker install."

#
# guest-exec is asynchronous: it returns {"return":{"pid":N}}, NOT the
# exitcode. The actual result comes from a follow-up guest-exec-status call
# keyed on that pid. Earlier revisions grepped for "exitcode":0 in the
# guest-exec response directly, which never matches regardless of the marker
# state — every poll falsely reported "still waiting" and Stage 8 always
# timed out.
DONE=0
for i in $(seq 1 "$CLOUD_INIT_POLLS"); do
   EXEC_RESP=$(virsh --connect qemu:///system qemu-agent-command "$DOM" \
      '{"execute":"guest-exec","arguments":{"path":"/usr/bin/test","arg":["-f","/var/lib/aes-ci-cloud-init-done"],"capture-output":true}}' \
      2>/dev/null || true)
   PID=$(echo "$EXEC_RESP" | sed -n 's/.*"pid":\([0-9]*\).*/\1/p')
   if [ -n "$PID" ]; then
      STATUS_RESP=$(virsh --connect qemu:///system qemu-agent-command "$DOM" \
         "{\"execute\":\"guest-exec-status\",\"arguments\":{\"pid\":$PID}}" \
         2>/dev/null || true)
      # Use printf '%s\n' instead of echo for $STATUS_RESP / $KVER below: the
      # JSON response can contain backslash escapes that bash's echo may
      # mis-handle under xpg_echo, and uname-r output is treated the same way
      # for consistency. Same defensive substitution as scripts/ci/check-dmesg.sh.
      if printf '%s\n' "$STATUS_RESP" | grep -q '"exitcode":0'; then
         DONE=1
         break
      fi
   fi
   if [ $((i % 6)) -eq 0 ]; then   # every minute
      echo_info "  still waiting (${i}/${CLOUD_INIT_POLLS} polls, ~$((i * CLOUD_INIT_POLL_INTERVAL))s elapsed)"
   fi
   sleep "$CLOUD_INIT_POLL_INTERVAL"
done

if [ "$DONE" -ne 1 ]; then
   echo_fail "cloud-init did not complete within $((CLOUD_INIT_POLLS * CLOUD_INIT_POLL_INTERVAL))s"
   echo_fail "  Serial log tail (last 80 lines):"
   tail -80 "$SERIAL_LOG" 2>/dev/null | sed 's/^/    /'
   exit 4
fi
echo_step "cloud-init completed; guest reports marker present"

# ----------------------------------------------------------------------------
# Stage 9 — kernel + docker verification
# ----------------------------------------------------------------------------
# Kernel check: uname -r must contain 'azure' (guest booted linux-image-azure).
# Docker check: Server version must be 28.x+ (from get.docker.com, not distro).
# Guest IP discovery via qemu-guest-agent (dhcp lease parsing is brittle).
# Small retry loop because the agent occasionally lags the kernel's dhcp lease
# by a few seconds.
echo_header "Stage 9 / 10: verify Azure kernel + Docker 28.x+"

GUEST_IP=""
for _ in $(seq 1 15); do
   # domifaddr lists lo (127.0.0.1), enp1s0 (libvirt NAT lease), and docker0
   # (172.17.x after docker-ce install). We want enp1s0 specifically — skip
   # lo/docker0/virbr0 by the interface name rather than the first-match.
   GUEST_IP=$(virsh domifaddr --source agent "$DOM" 2>/dev/null | awk '/^ enp[0-9]|^ eth[0-9]/ && /ipv4/ {print $4}' | cut -d/ -f1 | head -1)
   if [ -n "$GUEST_IP" ]; then break; fi
   sleep 2
done
if [ -z "$GUEST_IP" ]; then
   echo_fail "Could not determine guest IP via qemu-guest-agent"
   exit 3
fi
echo_info "Guest IP: $GUEST_IP"

SSH_CMD="ssh -i $SSH_KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5 ubuntu@$GUEST_IP"

# Kernel check: kernel contains 'azure'. Retry loop handles the window
# between cloud-init marker (written pre-reboot) and the post-reboot SSH
# coming up on the Azure kernel. Common in parallel provisioning where all
# VMs race.
KVER=""
for _ in $(seq 1 30); do
   KVER=$($SSH_CMD 'uname -r' 2>/dev/null || true)
   if printf '%s\n' "$KVER" | grep -q azure; then break; fi
   sleep 5
done
echo_info "Guest kernel: $KVER"
if ! printf '%s\n' "$KVER" | grep -q azure; then
   echo_fail "Kernel check failed — kernel is '$KVER' (expected contains 'azure')"
   exit 1
fi

# Docker check: server version must be >= 28 (upstream get.docker.com).
# Retry loop mirrors the kernel check — Docker may still be starting after
# reboot.
DVER=""
DMAJOR=""
for _ in $(seq 1 15); do
   DVER=$($SSH_CMD 'docker version --format "{{.Server.Version}}"' 2>/dev/null || true)
   DMAJOR=$(echo "$DVER" | cut -d. -f1)
   if [ "$DMAJOR" -ge 28 ] 2>/dev/null; then break; fi
   sleep 3
done
echo_info "Guest docker server version: $DVER"
if ! [ "$DMAJOR" -ge 28 ] 2>/dev/null; then
   echo_fail "Docker check failed — server version is '$DVER' (expected >= 28.x from get.docker.com)"
   exit 2
fi

echo_step "Azure kernel ($KVER) + Docker ($DVER) verified"

# ----------------------------------------------------------------------------
# Stage 10 — record provisioning marker
# ----------------------------------------------------------------------------
# Marker is the last thing written so a mid-provision interruption leaves no
# stale "done" state. Next run's Stage 4 hash check will either reuse this
# marker or invalidate it on user-data.yaml edit.
echo_header "Stage 10 / 10: record provisioning marker"
touch "$MARKER_FILE"
echo_step "VM provisioned and verified: $DOM @ $GUEST_IP (hash=${USER_DATA_HASH:0:12})"
echo_info "  Connect:       ssh -i $SSH_KEY ubuntu@$GUEST_IP"
echo_info "  Serial log:    $SERIAL_LOG"
echo_info "  Marker file:   $MARKER_FILE"
exit 0
