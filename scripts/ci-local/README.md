# Local CI Parity Harness

Reproducible Azure-kernel KVM guest for running the GitHub Actions CPU matrix
locally. One command, same kernel family as the GitHub runner, same Docker
version, same matrix container images.

## One-Command Contract

```
./scripts/run_ci_parity.sh
```

First run: ~15-20 minutes (downloads ~800 MB base cloud image, installs Azure
kernel in the guest, reboots, installs docker-ce, captures provisioning
marker). Subsequent runs: seconds — existing snapshot is detected and reused.

## Prerequisites

- x86_64 Linux host with CPU virtualization enabled in firmware (VT-x / AMD-V)
- KVM accessible: `/dev/kvm` readable/writable by your user
- Nested virtualization enabled if the host is itself a VM
- ~10 GB free disk under `~/.cache/aes-ci-parity/`
- Network egress to `cloud-images.ubuntu.com` (base image) and
  `download.docker.com` / `get.docker.com` (guest Docker install)

If any of the first three are missing, `scripts/ci-local/preflight_kvm.sh`
identifies which one and prints the fix.

## Install Host Packages

### Ubuntu 22.04 / 24.04 / Debian 12+

```bash
sudo apt-get update && sudo apt-get install -y \
   qemu-kvm libvirt-daemon-system libvirt-clients \
   bridge-utils virtinst cloud-image-utils \
   virtiofsd openssh-client wget
```

### Rocky Linux 9 / RHEL 9

```bash
sudo dnf install -y epel-release && \
sudo dnf install -y \
   qemu-kvm libvirt virt-install \
   cloud-utils-growpart cloud-utils \
   virtiofsd bridge-utils openssh-clients wget
```

After installing, add yourself to the libvirt and kvm groups, then log out
and back in:

```bash
sudo usermod -aG kvm,libvirt $(id -un)
```

`scripts/ci-local/install-host-deps.sh` runs either the apt-get or dnf block
above depending on `/etc/os-release`. If you invoke it as a non-root user it
prints the commands to run rather than elevating silently.

## What the Harness Does

1. **Preflight** — verifies `/dev/kvm` is present, readable, and nested virt
   is on. Fails fast with an actionable message identifying which of the
   three is missing.
2. **Host deps** — confirms qemu-kvm, libvirt, virt-install, cloud-localds,
   ssh, wget/curl are available. Non-root: prints install commands. Root:
   installs them.
3. **Base image** — downloads `noble-server-cloudimg-amd64.img` once to
   `~/.cache/aes-ci-parity/base-noble.qcow2`.
4. **Overlay + seed** — creates a qcow2 overlay backed by the base image and
   a cloud-init NoCloud seed ISO from `scripts/ci-local/cloud-init/*.yaml`
   with your SSH pubkey substituted in.
5. **virt-install --import** — registers a libvirt domain named `aes-ci`,
   boots it, waits for cloud-init to install `linux-image-azure`, reboot into
   it, install docker-ce 28.x from upstream, and write
   `/var/lib/aes-ci-cloud-init-done`.
6. **Hang reproduction** — runs the ubuntu:24.04 insmod chain inside a
   `docker run --privileged --security-opt label=disable` container in the
   guest. Captures sysrq-t, dmesg, and `/proc/<pid>/stack` if it hangs.
7. **CPU matrix execution** (`--matrix`) — runs the GitHub Actions CPU
   matrix cell-by-cell inside the parity VM over SSH + `docker run
   --privileged --security-opt label=disable -v /mnt/aes-stream-drivers:/work`.
   The current configuration ships **one container** (`ubuntu:24.04`) in both build-only
   (`load_test=false`) and load-test (`load_test=true`) modes, proving the
   virtiofs mount + docker passthrough mechanism. Each cell's
   stdout+stderr is teed into
   `logs/<YYYYMMDD-HHMMSS>/<sanitized-container>/cpu-<mode>.log`.

## Flags

- `--reset` — destroy + re-provision the VM (use after editing
  `cloud-init/user-data.yaml`, or if the guest has been wedged by a stuck
  insmod)
- `--no-hang-repro` — skip the hang-reproduction step (useful when you just
  want a running guest to SSH into)
- `--matrix` — after provisioning (and hang-repro, unless
  `--no-hang-repro`), execute the CPU matrix via
  `scripts/ci-local/run_matrix.sh`. Per-cell logs land under
  `logs/<ts>/<sanitized-container>/cpu-<mode>.log`. One failing cell does
  not abort the rest (`fail-fast: false` parity with GitHub Actions).

## Running the Matrix

```
./scripts/run_ci_parity.sh --matrix
```

Runs Stages 1-5 end-to-end: preflight -> host-deps -> provision -> hang-repro ->
CPU matrix. If the VM is already provisioned with a current user-data hash,
Stages 1-3 idempotent-skip in seconds.

### Expected Runtime

- **First run ever (new user-data):** ~15-20 min. The
  user-data.yaml change invalidates the idempotency
  marker, forcing a full re-provision (Azure kernel install + reboot +
  Docker install + cloud-init completion). This is the correct, documented
  behavior — see the troubleshooting entry on "First run after upgrading
  takes 15-20 min" below.
- **First matrix cell after provisioning:** +2-5 min for `docker pull
  ubuntu:24.04` cold-start. Subsequent cells reuse the cached image.
- **Per-cell wall-clock budget:** 15 min (the `timeout 900s` outer belt on
  `run_cell.sh`). A hang trips this budget with exit 124 or 137.
- **Steady-state matrix (cached image, provisioned VM):** ~3-5 min for the
  ubuntu:24.04 build-only cell, ~5-10 min for the load-test cell.

Skip re-provisioning if you already have a VM: run
`bash scripts/ci-local/enable_virtiofs.sh` once to hot-add virtiofs
(~2 min, one libvirt domain restart), then append the fstab line inside
the guest manually OR accept the one-time 15-min re-provision via
`./scripts/run_ci_parity.sh --reset`.

### Re-running One Cell

The harness does not yet have a `--cell <name>` flag (deferred to v2).
To iterate on a single cell, invoke `run_cell.sh` directly:

```
bash scripts/ci-local/run_cell.sh --container ubuntu:24.04 --load-test 0
bash scripts/ci-local/run_cell.sh --container ubuntu:24.04 --load-test 1
```

Outputs land in the same `logs/<ts>/<sanitized>/` tree. Because each run
uses a fresh timestamp, no previous-run collision is possible. Override
with `--log-dir PATH` if you want a stable target for CI-style diffing.

### Cleaning Up

```
# Remove per-cell log captures (safe — they're gitignored):
rm -rf logs/

# Remove build artifacts that landed in the host checkout via virtiofs
# (container root owns them; see Troubleshooting below):
sudo rm -rf install/ data_dev/driver/*.ko emulator/driver/*.ko data_dev/app/bin/*

# Destroy the parity VM entirely:
./scripts/run_ci_parity.sh --clean
```

## scripts/ci/*.sh — the Workflow's Scripts

**`scripts/ci/*.sh` are the GitHub Actions workflow's scripts. Never edit
them as part of local-CI work.** If the local harness needs different
behavior, fix the harness — not the script.

These files are the authoritative CI logic; the GitHub Actions runner and
the local parity VM both invoke them by exact path. The entire point of
this milestone is that the same script produces the same result in both
environments. The moment a local script is edited to "make it work
locally," the two environments diverge and the harness's reason for existing
disappears.

**If you find a real bug in `scripts/ci/*.sh`:** fix it in a commit that
ALSO updates `.github/workflows/ci_pipeline.yml` if needed, and verify
both GitHub and the local harness still pass. Do not silently fork.

**If the local harness can't meet the script's contract:** the harness is
wrong. Teach `scripts/ci-local/run_cell.sh` to provide the environment
(cwd, env vars, `/tmp` side-channel) the script expects. Every
`scripts/ci/*.sh` uses relative paths (cwd = repo root), reads its env
from defaults with no required external variables, and passes state
between steps via `/tmp` files that live in the same container. These
contracts are stable — the harness bends, not the scripts.

## SELinux on RHEL Hosts

S3DF RHEL workstations run SELinux in enforcing mode. Inside the parity
guest, every `docker run` the harness emits carries
`--security-opt label=disable` alongside `--privileged`. Without that flag,
`init_module()` is blocked at the LSM layer with `avc: denied { module_load }`
in dmesg (not a missing capability). This is why the flag appears — scripts
run inside the container and don't invoke `docker run` themselves.

## Troubleshooting

### `uname -r` in the guest still reports `generic`

`apt install linux-image-azure` failed silently during cloud-init. Usually an
APT archive DNS hiccup. Run with `--reset` to re-provision. If persistent,
check `/var/log/cloud-init-output.log` on the guest.

### `virt-install` hangs at GRUB

You omitted `--import`. `scripts/ci-local/provision_vm.sh` always includes
`--import`; don't hand-edit the virt-install invocation.

### `insmod: ERROR: could not insert module: Operation not permitted`

SELinux is blocking the container. Verify every `docker run` in your ad-hoc
invocation has BOTH `--privileged` AND `--security-opt label=disable`. If
triggered inside the harness, check `dmesg` on the guest for
`avc: denied { module_load }`.

### Guest is in a D-state zombie after a hang

Run `./scripts/run_ci_parity.sh --reset`. Signal delivery to a D-state task
is queued; SIGKILL cannot unstick it. Destroying and re-provisioning the
qcow2 overlay is the only clean path.

### Nested virt is off

If the host is itself a VM, the outer hypervisor must expose nested KVM.
`/sys/module/kvm_intel/parameters/nested` (or `kvm_amd`) must report `Y` or
`1`. Without nested, the guest falls back to TCG and is too slow to use as a
pre-push gate.

### Build artifacts are owned by root after a matrix run

virtiofs uses `accessmode='passthrough'`: container-root writes back to the
host checkout with UID 0. The host user can read `.ko` files but cannot
`rm` them without sudo. This is intentional — the container holds
CAP_SYS_MODULE so `insmod` works, and that capability requires root
inside the container.

Two clean-up paths:

```
# Let make clean run inside the container as root:
bash scripts/ci-local/run_cell.sh --container ubuntu:24.04 --load-test 0
# (or just re-run the matrix; make clean runs at the start of each cell)

# Or manually remove on the host:
sudo rm -rf install/ data_dev/driver/*.ko emulator/driver/*.ko data_dev/app/bin/*
```

A future release may revisit via virtiofsd's `--translate-uid`/`--translate-gid`
options when libvirt exposes them (not currently scoped).

### Virtiofs mount is empty on SLAC S3DF (NFS root_squash)

libvirtd spawns `virtiofsd` as `libvirt-qemu` (uid 64055). On NFS mounts
with `root_squash`, this UID maps to `nobody`, which has no read access
to your checkout under `$HOME`. Symptom: `ls /mnt/aes-stream-drivers`
inside the guest returns empty, and `run_cell.sh` exits 3 with a virtiofs
mount sanity message.

Fix by bind-mounting the checkout to a non-NFS path once per host:

```
sudo mkdir -p /var/tmp/aes-ci-mirror
sudo mount --bind /path/to/your/checkout /var/tmp/aes-ci-mirror
export AES_CI_REPO_SOURCE=/var/tmp/aes-ci-mirror
./scripts/run_ci_parity.sh --reset --matrix
```

Add the `export` to your shell rc file (or a per-project env-loader) so
every matrix run picks it up. Alternatively, `chmod o+rx` on your home
dir and checkout widens attack surface but avoids the bind-mount — not
recommended.

### `docker run` fails with "No such file or directory: scripts/ci/install-deps.sh"

The virtiofs mount came up after the container started. cloud-init's
`nofail` fstab flag lets the guest boot even if virtiofsd isn't ready;
`run_cell.sh` probes and retries `sudo mount -a` once. If the retry
fails, you'll see exit 3 with an infra-error message.

Usually the fix is patience — reboot the guest once:

```
virsh reboot aes-ci
# wait ~30 s, then re-run the matrix:
./scripts/run_ci_parity.sh --matrix
```

If the mount is permanently broken (virtiofsd not running on the host),
check `journalctl -u libvirtd` for virtiofsd errors and confirm the
filesystem element is present:

```
virsh dumpxml aes-ci | grep -A 2 virtiofs
```

Missing? Run `bash scripts/ci-local/enable_virtiofs.sh` to retrofit.

### First run after upgrading takes 15-20 min

The `scripts/ci-local/cloud-init/user-data.yaml` includes a virtiofs
fstab entry. The provisioner hashes user-data.yaml and keys the
idempotency marker by that hash. When the hash changes,
the existing VM must be torn down and re-provisioned so cloud-init can
apply the new first-boot configuration. This is the correct behavior —
not a bug.

To avoid the 15-min cost when your VM is otherwise fine, run the
retrofit tool:

```
bash scripts/ci-local/enable_virtiofs.sh
# Then manually add the fstab line inside the guest:
GUEST_IP=$(virsh domifaddr --source agent aes-ci | awk '/ enp/ && /ipv4/ {print $4}' | cut -d/ -f1 | head -1)
ssh -i ~/.cache/aes-ci-parity/id_ed25519 ubuntu@$GUEST_IP \
   "echo 'aes-stream-drivers /mnt/aes-stream-drivers virtiofs rw,nofail 0 0' | sudo tee -a /etc/fstab && sudo mkdir -p /mnt/aes-stream-drivers && sudo mount -a"
```

New contributors provisioning fresh do NOT need `enable_virtiofs.sh` —
`provision_vm.sh` Stage 7 bakes virtiofs into the initial `virt-install`
invocation.

## Environment Variables

- `AES_CI_BASE_IMAGE_URL` — override base cloud image URL (default:
  `https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img`).
  Useful if your site has an internal Ubuntu mirror.
- `AES_CI_CACHE_DIR` — override cache location (default:
  `$HOME/.cache/aes-ci-parity`).
- `AES_CI_VM_MEMORY` — VM RAM in MB (default: 4096).
- `AES_CI_VM_VCPUS` — VM vCPU count (default: 4).
- `AES_CI_SERIAL_LOG` — path where libvirt captures the guest serial console
  (default: auto-detects; falls back to `/tmp/aes-ci-${DOM}-serial.log` when
  `$AES_CI_CACHE_DIR` is on NFS, else `$CACHE_DIR/${DOM}-serial.log`). Explicit
  override needed when libvirtd cannot open the auto-detected path (e.g.,
  SLAC S3DF and other NFS `root_squash` setups where root→nobody denies
  reads of any file under `$HOME`).
- `AES_CI_REPO_SOURCE` — host-side path shared into the guest as the
  virtiofs source. Defaults to `git rev-parse --show-toplevel` (or `$PWD`
  outside a git tree). Override to a local-disk path on SLAC S3DF / NFS
  `root_squash` hosts — see the troubleshooting entry above. The path
  must be readable by `libvirt-qemu` (uid 64055).
- `AES_CI_GUEST_MOUNT` — mount path inside the guest for the virtiofs
  share (default: `/mnt/aes-stream-drivers`). Rarely overridden;
  `run_cell.sh` uses this in the `docker run -v $GUEST_MOUNT:/work` bind.
- `AES_CI_CELL_TIMEOUT_SEC` — outer per-cell wall-clock budget in seconds
  (default: 900 = 15 min). This is the **belt** — `scripts/ci/load-modules-cpu.sh`'s
  120s-per-insmod timeout is the **suspenders**. Lower this for
  faster-fail iteration; raise it only if your host's network pulls
  `docker pull ubuntu:24.04` slowly enough to blow 15 min on cold start.

## Related Scripts

- `scripts/ci-local/lib/common.sh` — shared bash helpers (echo_*, SUDO detect)
- `scripts/ci-local/preflight_kvm.sh` — KVM three-way preflight
- `scripts/ci-local/install-host-deps.sh` — host distro package installer
- `scripts/ci-local/provision_vm.sh` — VM lifecycle (download, overlay, seed,
  virt-install, wait)
- `scripts/ci-local/hang_repro.sh` — drives the ubuntu:24.04 insmod chain
  inside the guest and captures diagnostics on hang
- `scripts/ci-local/capture_diag.sh` — sysrq-t, dmesg, `/proc/<pid>/stack`
  collector
- `scripts/ci-local/run_cell.sh` — per-cell primitive (one `docker run`
  on the guest; takes `--container IMG --load-test 0|1`)
- `scripts/ci-local/run_matrix.sh` — sequential matrix loop over
  `CELLS[]`; bitwise-OR exit-code aggregation; `--parallel`
  stub exits 1
- `scripts/ci-local/enable_virtiofs.sh` — migration tool: retrofits
  virtiofs onto an existing VM without the 15-min re-provision
  cost. New contributors do not need this
