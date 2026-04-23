# AI Agent: Local KVM CI Testing

This file instructs AI coding agents (Claude Code, Cursor, Copilot, etc.)
to run local KVM CI validation when modifying kernel module source code.

## When to run local CI

Run local KVM CI testing after making changes to any of these paths:

- `emulator/driver/src/` — emulator kernel module
- `common/driver/` — shared DMA/buffer/axis kernel code
- `data_dev/driver/src/` — datadev kernel module
- `emulator/gpu_stub/src/` — GPU stub module (affects `--phase gpu`)
- `scripts/ci/` — CI pipeline scripts

Do NOT skip local CI testing in favor of "it compiles" or "grep confirms
the change." Compilation does not prove the module loads, the driver probes,
or DMA transfers succeed. The KVM parity harness runs the same scripts as
GitHub Actions CI on the same Azure kernel family.

## Prerequisites check

```bash
bash scripts/ci-local/preflight_kvm.sh
```

Exit 0 means KVM is usable. Non-zero prints what's missing.

Also confirm the user is in the right groups (should be true on the reference
host; check if commands below fail with permission errors):

```bash
id -Gn | tr ' ' '\n' | grep -E '^(kvm|libvirt|docker)$' | sort -u
# Expect: docker, kvm, libvirt — all three
```

If any are missing, `virsh start`, `docker run`, or `/dev/kvm` access will
need `sudo`. Ask the user to add you to the missing groups rather than
running the harness under `sudo` (the scripts are written to run unprivileged
when groups are set).

## Step 0: Repo source sync (MANDATORY on S3DF / NFS hosts)

**The aes-ci VM does not necessarily mount your working tree.** At VM
provision time, `provision_vm.sh` resolves `AES_CI_REPO_SOURCE` (default:
`git rev-parse --show-toplevel`, override via env var). On NFS root_squash
hosts (SLAC S3DF), this is typically set to a local-disk path like
`/sdf/group/faders/users/<user>/project/temp/aes-stream-drivers` because
libvirt-qemu (uid 64055) cannot read files under `$HOME`.

**Consequence:** If you commit code in `~/temp/aes-stream-drivers` and run
the CI cell, the VM will build from the provisioning-time path — which may
be a *different* checkout at an older commit. The cell will PASS against
stale code and the dmesg will lack any strings you just added. This is
indistinguishable from a successful run unless you look closely.

Detect and sync before every CI run:

```bash
VM_SRC=$(virsh dumpxml "${AES_CI_DOMAIN:-aes-ci}" 2>/dev/null | \
    awk -F"'" "/<filesystem/,/<\\/filesystem>/ {if (/source dir=/) {print \$2; exit}}")
MY_SRC=$(git rev-parse --show-toplevel)

echo "VM mounts:  $VM_SRC"
echo "My tree:    $MY_SRC"

if [ "$VM_SRC" != "$MY_SRC" ] && [ -d "$VM_SRC/.git" ]; then
   # One-time: add your working tree as a local remote in the VM-mounted checkout.
   git -C "$VM_SRC" remote get-url home >/dev/null 2>&1 || \
      git -C "$VM_SRC" remote add home "$MY_SRC"
   # Fetch your commits and fast-forward the VM-mounted checkout.
   git -C "$VM_SRC" fetch home
   BRANCH=$(git -C "$MY_SRC" rev-parse --abbrev-ref HEAD)
   git -C "$VM_SRC" checkout "$BRANCH" 2>/dev/null || \
      git -C "$VM_SRC" checkout -b "$BRANCH" "home/$BRANCH"
   git -C "$VM_SRC" merge --ff-only "home/$BRANCH"
   echo "Synced $VM_SRC to $(git -C "$VM_SRC" rev-parse --short HEAD)"
fi
```

If `$VM_SRC` and `$MY_SRC` are the same path, this block is a no-op and
you can proceed directly to pre-run hygiene.

### Hot-loop iteration shortcut (uncommitted edits)

The git-remote-ff sync above assumes you have committed your changes.
For fast iteration on a single file — e.g. diagnosing a test failure by
editing one `.c` or `.cpp` and re-running — copy the file directly:

```bash
cp "$MY_SRC/emulator/driver/src/gpu_engine.c" "$VM_SRC/emulator/driver/src/gpu_engine.c"
```

The docker container's `cp -a /src/. /work/` will pick up the new bytes
on the next run. Reserve the git-ff sync for end-of-iteration verification
and commit/push; the per-file copy is much faster when you're in a
diagnose → fix → retest loop and don't want to burn a commit on each try.

## Pre-run hygiene (mandatory)

**Assume a stale KVM is already running from a prior session.** A leftover
`qemu-system-x86_64` process will hold the old emulator `.ko` open, mask
rebuild failures, and produce log output from code that no longer exists
on disk. Always perform this sequence before any `make clean && make` or
KVM invocation:

```bash
# 1. Find and kill stale QEMU/libvirt KVMs
pgrep -af qemu-system-x86_64           # inspect — confirm what you're about to kill
virsh list --all                       # libvirt domains (run_ci_parity.sh uses these)
virsh destroy aes-ci 2>/dev/null || true   # stop the libvirt domain if present
pkill -f qemu-system-x86_64 || true        # belt-and-suspenders for bare QEMU

# 2. Clean rebuild (host-side, mostly diagnostic — the real build happens
#    inside the container; see "Post-run verification" below for why).
make -C emulator/driver clean
make -C emulator/driver
```

**Why:** `EMU_BUILD_VERSION` is stamped into `emulator/driver/Makefile` via
`-DEMU_BUILD_VERSION=$(shell date +%s)` and printed by the module at probe
time as `build_version=<N>`. That stamp is the single ground-truth link
between the source-tree snapshot inside the VM and the test run's dmesg.
Without the pkill + sync (Step 0) + clean rebuild, the KVM may reuse a
cached `.ko` or a stale source tree, and "PASS" results can come from code
that is no longer in `src/`.

## Quick validation — CPU phase (build + load + test on Ubuntu 24.04)

```bash
# Provision VM if not already running (idempotent — skips if current)
bash scripts/run_ci_parity.sh --no-hang-repro

# Run one load-test cell on Ubuntu 24.04 with Azure kernel
bash scripts/ci-local/run_cell.sh --container ubuntu:24.04 --load-test 1 --phase cpu
```

This builds the modules inside a Docker container on the KVM guest
(kernel 6.17+ Azure), loads them via insmod, runs the test suite
(ioctl / file-ops / error-paths / multichannel / proc / data_integrity),
and checks dmesg for oops/panic/warnings. Takes ~5-10 minutes.

## Quick validation — GPU phase

```bash
bash scripts/ci-local/run_cell.sh --container ubuntu:24.04 --load-test 1 --phase gpu
```

The GPU phase drives `scripts/ci/load-modules-gpu.sh` and `test-gpu.sh`,
which additionally load `nvidia_p2p_stub.ko` before `datadev_emulator.ko`
and expect the async-GPU code path to be exercised. Success requires the
stub module to register `emu_gpu_addr_lookup` strongly before the
emulator's RX/TX ticks would otherwise no-op via `__weak` fallback.

## CPU vs GPU cheat sheet

| Concern | CPU phase | GPU phase |
|---|---|---|
| Module load order | `datadev_emulator` then `datadev` | `nvidia_p2p_stub` then `datadev_emulator` then `datadev-gpu` |
| Key dmesg success marker | `datadev_emulator: emulator loaded successfully` | Same + `emu: BAR0 GPU Async V4 initialized (version=4, maxBuffers=N)` + `gpu engine poll thread starting` |
| ioctl probe | `GPU_Is_Gpu_Async_Supp = 0` | `GPU_Is_Gpu_Async_Supp = 1` |
| Driver node | `/dev/datadev_0` | `/dev/datadev_0` + `/dev/nvidia_p2p_stub_mem` (miscdevice) |
| Success indicator in log | `=== Results: 6 passed, 0 failed ===` | `ALL GPU TESTS PASSED` |
| Typical runtime | 5-10 min | 8-15 min |

## Post-run verification

After every KVM completion, cross-check the build version the guest
actually loaded. `build_version=` is only in the VM's dmesg ring buffer —
it does NOT appear in the cell log (which captures docker stdout only).

```bash
# Get the VM IP and SSH key path
VM_IP=$(virsh domifaddr --source agent "${AES_CI_DOMAIN:-aes-ci}" 2>/dev/null \
        | awk '/ipv4/ {split($4,a,"/"); print a[1]; exit}')
SSH_KEY="$(bash -c '. scripts/ci-local/lib/common.sh && echo "$AES_CI_CACHE_DIR_RESOLVED"')/id_ed25519"
SSH="ssh -i $SSH_KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null ubuntu@$VM_IP"

# Grep build_version from the VM's dmesg
$SSH "sudo dmesg | grep 'build_version=' | tail -1"

# Compare against the Makefile stamp currently on the VM-mounted tree
VM_SRC=$(virsh dumpxml "${AES_CI_DOMAIN:-aes-ci}" 2>/dev/null | \
         awk -F"'" "/<filesystem/,/<\\/filesystem>/ {if (/source dir=/) {print \$2; exit}}")
git -C "$VM_SRC" log -1 --format="%h %s" -- emulator/driver/
```

The build_version stamp comes from the **container's** `date +%s` at build
time, so it will be later than any host-side `date +%s` at the start of
the run. That is expected — what matters is that it is **not older** than
the moment you ran pre-run hygiene (which would indicate a cached build).

Treat any dmesg that does NOT match the source tree currently at `$VM_SRC`
as a hard failure — it means the virtiofs sync or cache invalidation is
broken. Do not file bugs, commit fixes, or trust pass/fail signals from
such a run.

## Running ad-hoc module tests

For boundary cases, parameter matrices, kthread inspection, or anything
not covered by `run_cell.sh`'s fixed flow, use this pattern. It mirrors
the CI harness (privileged + label=disable + cp-a from /src:ro into
writable /work) so the module builds and loads the same way:

**CPU-phase template** (builds only `emulator/driver`, loads only
`datadev_emulator` + `datadev`):

```bash
cat > /tmp/my-test.sh <<'SCRIPT'
#!/bin/bash
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq build-essential make kmod linux-headers-$(uname -r) >/dev/null 2>&1
cd /work

make -C emulator/driver 2>&1 | tail -5   # avoid `make -s` — hides errors
# ... your checks here, e.g.:
insmod emulator/driver/datadev_emulator.ko emu_gpu_max_buffers=8
sleep 1
grep '\[datadev_emulator\]' /proc/kallsyms | head -20
ps -eLf | grep emu_gpu_poll   # requires --pid=host below
rmmod datadev_emulator
SCRIPT

# Copy the script into the VM-mounted tree so the container can see it,
# then run inside a docker container shaped like the CI cell.
cp /tmp/my-test.sh "$VM_SRC/tmp-my-test.sh"
$SSH "docker run --rm --privileged --security-opt label=disable --pid=host \
    -v /mnt/aes-stream-drivers:/src:ro -v /lib/modules:/lib/modules:ro -w /work \
    ubuntu:24.04 bash -c 'cp -a /src/. /work/ && bash /work/tmp-my-test.sh'"
rm -f "$VM_SRC/tmp-my-test.sh"
```

**GPU-phase template** (adds `nvidia_p2p_stub` + `datadev`; mirrors
`scripts/ci/build-gpu.sh` + `load-modules-gpu.sh` ordering — deviate
from this order at your own risk, see the failure table below):

```bash
cat > /tmp/my-gpu-test.sh <<'SCRIPT'
#!/bin/bash
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq build-essential make kmod linux-headers-$(uname -r) g++ >/dev/null 2>&1
cd /work

# Build order: gpu_stub FIRST so the emulator's modpost finds
# emu_gpu_register_drain_cb / emu_gpu_unregister_drain_cb symbols.
make -C emulator/gpu_stub 2>&1 | tail -3
make -C emulator/driver   2>&1 | tail -5
make -C data_dev/driver NVIDIA_DRIVERS=/work/emulator/gpu_stub GITV=diag 2>&1 | tail -3
make -C data_dev/app all  2>&1 | tail -3

dmesg -C
# Load order: stub FIRST (exports drain_cb symbols), then emulator,
# then datadev. Inverting this produces "Unknown symbol ... (err -2)".
insmod emulator/gpu_stub/nvidia_p2p_stub.ko
insmod emulator/driver/datadev_emulator.ko   # add emu_gpu_debug_sc2=1 etc. here
insmod data_dev/driver/datadev.ko cfgTxCount=64 cfgRxCount=64 cfgSize=65536

# Docker has no udev — create both device nodes manually.
if [ ! -e /dev/datadev_0 ]; then
    MAJOR=$(awk '$2=="datadev_0"{print $1}' /proc/devices)
    mknod /dev/datadev_0 c "$MAJOR" 0 && chmod 666 /dev/datadev_0
fi
if [ ! -e /dev/nvidia_p2p_stub_mem ]; then
    MINOR=$(awk '$2=="nvidia_p2p_stub_mem"{print $1}' /proc/misc)
    mknod /dev/nvidia_p2p_stub_mem c 10 "$MINOR" && chmod 666 /dev/nvidia_p2p_stub_mem
fi

# ... your checks here, e.g. smoke test:
timeout 30 ./data_dev/app/bin/rdmaTestEmu -c 100
echo "RC=$?"

rmmod datadev
rmmod datadev_emulator
rmmod nvidia_p2p_stub
SCRIPT

cp /tmp/my-gpu-test.sh "$VM_SRC/tmp-my-gpu-test.sh"
$SSH "docker run --rm --privileged --security-opt label=disable --pid=host \
    -v /mnt/aes-stream-drivers:/src:ro -v /lib/modules:/lib/modules:ro -w /work \
    ubuntu:24.04 bash -c 'cp -a /src/. /work/ && bash /work/tmp-my-gpu-test.sh'"
rm -f "$VM_SRC/tmp-my-gpu-test.sh"
```

Key invariants (matching `scripts/ci-local/run_cell.sh`):

- `--privileged --security-opt label=disable` together on the same line
  (SELinux otherwise blocks `init_module`).
- `-v /mnt/aes-stream-drivers:/src:ro` + `cp -a /src/. /work/` — virtiofs
  is read-only from the container user; kernel `make` needs a writable
  source tree.
- `-v /lib/modules:/lib/modules:ro` — so the container's `insmod` and
  `make` can find the running kernel's build tree.
- **Do NOT bind-mount `/usr/src:ro`** — it will break dpkg when the
  container tries to install `linux-headers-$(uname -r)`. Let the
  container install its own headers fresh.
- `--pid=host` — required for any `ps`/`pgrep`/`/proc` inspection that
  needs to see kernel threads or host processes (see "Docker PID
  namespace trap" below).

### Ad-hoc first-run failure table

These failures have all bitten previous sessions. Check here before
diving into a diagnostic if something goes wrong on the first run of
an ad-hoc test:

| Symptom | Root cause | Fix |
|---|---|---|
| `modpost: "emu_gpu_register_drain_cb" [datadev_emulator.ko] undefined!` (and similar `emu_gpu_unregister_drain_cb`) | Building `emulator/driver` before `emulator/gpu_stub` — `KBUILD_EXTRA_SYMBOLS` has no `Module.symvers` to read | Build `gpu_stub` **first**, then `emulator/driver`; see the GPU-phase template above |
| `insmod: ERROR: ... Unknown symbol emu_gpu_register_drain_cb (err -2)` at insmod time | Loading `datadev_emulator.ko` before `nvidia_p2p_stub.ko` — stub exports the symbol | Load order: `nvidia_p2p_stub → datadev_emulator → datadev` (mirrors `load-modules-gpu.sh`) |
| `rdmaTestEmu: open(/dev/nvidia_p2p_stub_mem) failed: No such file or directory`, even though dmesg says the miscdevice was registered | No udev inside the container; miscdevices don't get a node automatically | `mknod /dev/nvidia_p2p_stub_mem c 10 $(awk '$2=="nvidia_p2p_stub_mem"{print $1}' /proc/misc)` |
| Build dies with only `make: *** [Makefile:248: __sub-make] Error 2` and no visible compiler diagnostic | Silent-mode `make -s` swallowed the real error | Re-run without `-s`; pipe through `tail -20` so the compiler error is visible |
| Test passes but dmesg shows the old `build_version=` stamp | The container's `cp -a /src/. /work/` picked up a stale VM_SRC tree — you edited `$MY_SRC` but forgot to sync | Either run the Step 0 git-ff block, or use the hot-loop `cp` shortcut from Step 0 |

### SC verification one-liner

When you need to smoke-test that rdmaTestEmu loopback still
works end-to-end (all four ROADMAP success criteria), run these inside
the GPU-phase template above (after `insmod` + mknod):

```bash
./data_dev/app/bin/rdmaTestEmu -c 100              # single-size loopback
./data_dev/app/bin/rdmaTestEmu -c 100 -b 4         # 4-buffer round-robin
./data_dev/app/bin/rdmaTestEmu -c 100 --sweep      # payload matrix (order=8 at 1MB)
# Fault-injection: modprobe datadev_emulator emu_gpu_poll_interval_us_min=10000000,
# then run `rdmaTestEmu -c 100` and expect RC=1 with stderr containing
# "rx doorbell timeout buf=N elapsed=10.0s" within ~10s (not a hang).
```

All three positive-path invocations should exit `RC=0`; dmesg should
have zero `addr lookup NULL` lines and zero `non-contiguous GPU memory
detected` warnings. The full-fidelity reproducer lives in the verification documentation.

## Inspecting VM state (SSH into the guest)

Once `run_ci_parity.sh --no-hang-repro` has provisioned/started the VM,
the guest is reachable via SSH. Cache the connection details early so you
can inspect `dmesg`, `/proc`, `ps`, sysfs, etc. whenever you need to:

```bash
VM_IP=$(virsh domifaddr --source agent "${AES_CI_DOMAIN:-aes-ci}" \
        | awk '/ipv4/ {split($4,a,"/"); print a[1]; exit}')

# The SSH key lives under AES_CI_CACHE_DIR_RESOLVED, which is NFS-aware.
# On S3DF it resolves to /var/tmp/aes-ci-parity-$USER, NOT $HOME/.cache.
SSH_KEY="$(bash -c '. scripts/ci-local/lib/common.sh && echo "$AES_CI_CACHE_DIR_RESOLVED"')/id_ed25519"

# From here, prefix any command you want to run on the guest with:
ssh -i "$SSH_KEY" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    ubuntu@"$VM_IP" "<command>"
```

Common inspection commands:

```bash
$SSH "sudo dmesg | tail -80"                                     # kernel log
$SSH "sudo dmesg | grep -E 'datadev_emulator|emu:'"              # project log lines
$SSH "lsmod | grep -E 'datadev|emu_|nvidia_p2p'"                 # loaded modules
$SSH "sudo grep '\[datadev_emulator\]' /proc/kallsyms | head"    # module symbols
$SSH "ls /sys/module/datadev_emulator/parameters/"               # module params (when loaded)
$SSH "cat /proc/datadev_0"                                       # driver status
```

## Reading cell logs

Logs are written to `logs/<YYYYMMDD-HHMMSS>/<distro>/<phase>-<mode>.log`.

Key patterns to check:

```bash
LOG=$(ls -t logs/*/ubuntu-24.04/cpu-load-test.log | head -1)

# Success indicators (in the log — captured from container stdout):
grep 'datadev_emulator is live'      "$LOG"   # load-modules-cpu.sh success
grep 'Mapped to'                     "$LOG"   # datadev ioremap succeeded
grep '=== Results.*passed'           "$LOG"   # test aggregator

# Failure indicators:
grep -E 'FAIL|panic|BUG:|oops|Cannot allocate|ERROR: could not' "$LOG"
```

For dmesg-only signals (build_version, prbs seed, BAR0 init, etc.), you
**must** SSH into the guest; the cell log does not contain dmesg.

## Cell exit-code legend

`run_cell.sh` exit codes (from `scripts/ci-local/run_cell.sh:34-53`):

| Exit | Meaning |
|------|---------|
| 0   | Cell passed (all pipeline steps exit 0) |
| N   | First non-zero exit from any chained `scripts/ci/*.sh` step |
| 3   | Infrastructure error — guest unreachable, virtiofs missing, SSH auth failed |
| 5   | Unsupported flag / missing required flag / usage error |
| 124 | Outer timeout fired SIGTERM — cell wall-clock exceeded (`AES_CI_CELL_TIMEOUT_SEC`, default 900s) |
| 137 | Outer timeout fired SIGKILL — `--kill-after=30s` tripped after SIGTERM |

`run_ci_parity.sh` aggregates these with offsets (exit 20-25 = provision
stage, 30 = hang reproduced, 40+ = CPU matrix cell failure, 50+ = GPU
matrix cell failure). See `scripts/run_ci_parity.sh:31-45` for the full table.

## Interpreting common failures

- **"Cannot allocate memory" from insmod datadev** — DMA buffer allocation
  failed. Check cfgMode (BUFF_STREAM vs BUFF_COHERENT) and cfgSize. The
  emulator module itself loaded fine; this is a driver-side allocation issue.

- **PAT/ioremap warnings in dmesg** — BAR0 memory type conflict. The
  emulator's alloc_pages path may need adjustment for the kernel version.

- **"insmod timed out"** — Module init hung. Capture diagnostics:
  `bash scripts/ci-local/capture_diag.sh`

- **Build failure on one distro but not others** — Kernel API difference.
  Check `#if LINUX_VERSION_CODE >= KERNEL_VERSION(...)` guards.

- **Cell PASSED but dmesg lacks your new strings** — You did not run
  Step 0. The VM built the old tree. Sync and rerun.

- **dpkg "Read-only file system" during `apt-get install linux-headers`** —
  You bind-mounted `/usr/src:ro` into an ad-hoc container. Remove that
  mount; let the container install its own headers.

- **`ps -eLf | grep emu_gpu_poll` returns nothing inside the container** —
  Docker PID namespace isolation. Add `--pid=host`; the kthread will then
  appear as `[emu_gpu_poll]`. See next section.

## Docker PID namespace trap

By default, a Docker container has its own PID namespace — processes on
the VM host (including all kernel threads) are invisible to `ps`, `pgrep`,
and `/proc/<pid>/` inside the container. Any runtime check that greps for
a kernel thread (`emu_gpu_poll`, `ksoftirqd`, etc.) **must** pass
`--pid=host` to `docker run`, otherwise the check silently returns zero
results and you get a false negative.

`run_cell.sh`'s test-suite steps don't need this because the tests drive
the module through ioctls, not by inspecting the host process tree.
Ad-hoc scripts you write should default to `--pid=host` if they touch
`ps`/`pgrep`/`/proc`.

## Full matrix validation

```bash
bash scripts/run_ci_parity.sh --no-hang-repro --matrix
```

Runs the full CPU + GPU matrix (Ubuntu 24.04, Ubuntu 22.04, Rocky 9,
Debian experimental) in both build-only and load-test modes. Takes
~20-30 minutes. Use this **before finalizing a phase or PR**, not during
iteration — single-cell runs are the fast path.

## Running all 10 cells (5 CPU + 5 GPU)

### Parallel mode (recommended when host has sufficient RAM/CPU)

`run_matrix.sh --parallel` provisions one KVM per load-test cell and runs
all cells concurrently. Each VM has its own kernel, eliminating the
`insmod`/`rmmod` cross-contamination that prevents single-VM parallelism.

**Important:** Run CPU and GPU phases sequentially (5 VMs at a time), not
all 10 simultaneously. Running 10 VMs × 2 vCPUs = 20 vCPUs on a 24-core
host leaves insufficient headroom — the emulator's DMA poll kthread
(≈1 ms cadence via `usleep_range(900, 1100)` in `dma_engine.c`) misses
DMA ring capture deadlines, producing widespread timeouts. The 5+5
workflow (CPU first, then GPU) uses 10 vCPUs and completes reliably.

```bash
# Parallel CPU matrix (provisions ~5 VMs, one per load-test cell):
bash scripts/ci-local/run_matrix.sh --phase cpu --parallel

# Parallel GPU matrix (reuses the same 5 VMs):
bash scripts/ci-local/run_matrix.sh --phase gpu --parallel
```

**Resource requirements:** Each VM needs **2 vCPUs and 3 GB RAM** for
reliable operation. Using 4 vCPUs per VM with 5 parallel VMs causes host
CPU contention — the emulator's DMA poll kthread misses ring capture
deadlines, producing insmod hangs in irq_modes and module-reload tests.
Using 2 GB RAM causes dpkg out-of-memory on ubuntu:24.04 during
linux-headers unpack.

**Note on GPU phase soak timing:** `scripts/ci/load-modules-gpu.sh`
overrides `emu_gpu_poll_interval_us` from its 1000 µs kernel default to
100 µs during CI, and `emu_gpu_start()` in `gpu_engine.c` promotes the
`emu_gpu_poll` kthread to `SCHED_FIFO(1)` via `sched_set_fifo_low()`.
Both are load-bearing for the 10 k-frame soak on 2-vCPU Azure runners:
userspace busy-spins on the doorbell word while the kthread writes it
from kernel space, and without RT preemption a contended CFS vCPU can
defer the `usleep_range` wakeup well past the test's 10-second
per-doorbell deadline. If you see `rdmaTestEmu: rx doorbell timeout
buf=N elapsed=10.0s`, this is the first thing to check.

The recommended configuration for 5 parallel VMs on a 24-core / 64+ GB host:

```bash
export AES_CI_PARALLEL_VM_MEMORY=3072   # 3 GB per VM (15 GB total)
export AES_CI_PARALLEL_VM_VCPUS=2       # 2 vCPUs per VM (10 cores total)
bash scripts/ci-local/run_matrix.sh --phase cpu --parallel
```

Provisioning N VMs takes ~5-10 minutes (parallelized, includes cloud-init
+ Azure kernel install + Docker install + reboot). After that, all cells
run concurrently — total wall-clock drops from ~60-90 minutes to ~15-20
minutes per phase. Per-cell timeout is 15 minutes (`AES_CI_CELL_TIMEOUT_SEC`).

VMs are named `aes-ci-0`, `aes-ci-1`, etc. After completion, slots 1+
are shut down automatically; slot 0 stays running for follow-up work.
Clean up with:

```bash
for i in $(seq 0 4); do virsh destroy "aes-ci-$i" 2>/dev/null; done
```

### Recommended parallel workflow

Run CPU and GPU phases sequentially, but cells within each phase in
parallel. The GPU phase reuses the already-provisioned VMs from the CPU
phase, so only the first phase incurs the ~5-10 minute provisioning cost.
Fix any failures before moving to the next phase:

```bash
export AES_CI_PARALLEL_VM_MEMORY=3072
export AES_CI_PARALLEL_VM_VCPUS=2

# 1. CPU phase (5 load-test cells + 1 build-only, parallel)
bash scripts/ci-local/run_matrix.sh --phase cpu --parallel
# → Fix any failures, rerun failing cells

# 2. GPU phase (reuses the same VMs if still provisioned)
bash scripts/ci-local/run_matrix.sh --phase gpu --parallel
# → Fix any failures, rerun failing cells

# 3. Commit and push
```

Total wall-clock for the full 5+5 run: ~30-40 minutes (vs. ~90+ minutes
sequential).

### Sequential mode (single VM, lower resource usage)

To cover all 5 distros × 2 phases (CPU + GPU) = 10 cells sequentially:

```bash
# Sequential CPU matrix:
bash scripts/ci-local/run_matrix.sh --phase cpu

# Or manual loop for custom distro selection:
DISTROS=("ubuntu:24.04" "ubuntu:22.04" "rockylinux:9" "debian:experimental" "fedora:rawhide")
for phase in cpu gpu; do
   for distro in "${DISTROS[@]}"; do
      echo "=== $distro ($phase) ==="
      bash scripts/ci-local/run_cell.sh --container "$distro" --load-test 1 --phase "$phase"
   done
done
```

Each cell takes ~5-10 minutes; the full 10-cell matrix takes ~60-90
minutes sequentially.

### Why can't a single VM parallelize load-test cells?

Load-test cells (`--load-test 1`) share the KVM guest's kernel via
`--privileged`. Each cell does `insmod` → test → `rmmod` of kernel
modules (`datadev`, `datadev_emulator`, `nvidia_p2p_stub`). Two
containers running `insmod`/`rmmod` against the same kernel
simultaneously will corrupt module state, produce `EEXIST`/`EBUSY`
errors, or trigger kernel oops.

Build-only cells (`--load-test 0`) can safely run in parallel on a
single VM because they only compile — no kernel module interaction.

## Cleanup & log retention

- **VM lifecycle:** Leave the VM running between cells; docker `--rm`
  resets container state each cell, and provisioning is the expensive
  step. Shut it down with `virsh shutdown aes-ci` only when you are done
  for the session.
- **Log directory:** `logs/` accumulates one timestamped subdirectory per
  cell invocation. There is no auto-pruning — periodically
  `rm -rf logs/<old-ts>` or keep only the most recent few runs.
- **Worktree cache:** Cloud image + seed ISO + SSH key live under
  `AES_CI_CACHE_DIR_RESOLVED` (typically `/var/tmp/aes-ci-parity-$USER/`
  on NFS hosts). Regenerating these costs ~2 minutes, so leave them
  unless you are debugging provisioning itself.

## Fallback: QEMU/TCG (no KVM required)

If KVM is unavailable (preflight fails), use the QEMU/TCG runner:

```bash
./run_local_ci.sh
```

This boots an Ubuntu 24.04 cloud image under pure software emulation.
Slower (~10-15 min) and tests only one kernel, but requires no KVM or
libvirt. Note: some Ubuntu cloud image kernels have pre-existing bugs
under TCG emulation unrelated to this project.

## Environment variables

- `AES_CI_REPO_SOURCE` — host path shared into the guest via virtiofs.
  Resolved at **provisioning time** and baked into the libvirt domain
  XML; changing it after provisioning requires `--reset`. On S3DF defaults
  to a local-disk copy of your repo, not your `$HOME` checkout.
- `AES_CI_CACHE_DIR` — Override cache dir location; NFS-aware resolution
  exported as `AES_CI_CACHE_DIR_RESOLVED` by `lib/common.sh`.
- `AES_CI_DOMAIN` — libvirt domain name (default: `aes-ci`).
- `AES_CI_VM_MEMORY` — Guest RAM in MB (default: 4096).
- `AES_CI_VM_VCPUS` — Guest vCPU count (default: 4).
- `AES_CI_CELL_TIMEOUT_SEC` — Per-cell wall-clock timeout (default: 900).
- `AES_CI_GUEST_MOUNT` — In-guest virtiofs mount path (default:
  `/mnt/aes-stream-drivers`).
- `AES_CI_SERIAL_LOG` — Override libvirt serial log path when NFS
  root_squash blocks the default location.

See `scripts/ci-local/README.md` for full reference.
