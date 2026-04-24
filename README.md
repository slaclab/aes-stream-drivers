# aes-stream-drivers

**[Documentation](https://slaclab.github.io/aes-stream-drivers)** | [DOE Code](https://www.osti.gov/doecode/biblio/8043)

Common repository for streaming kernel drivers (datadev, gpuDirect, Yocto, etc)

<!--- ########################################################################################### -->

#### common/

Contains shared kernel and application libraries

#### data\_dev/

Contains driver and application code for TID-AIR generic DAQ PCIe cards, optionally with GPUDirect RDMA support (for use with NVIDIA GPUs)

/etc/modprobe.d/datadev.conf

options datadev cfgTxCount=1024 cfgRxCount=1024 cfgSize=131072 cfgMode=1 cfgCont=1

#### include/

Contains top level application include files for all drivers

#### rce\_hp\_buffers/

Contains driver that allocates memory blocks for use in a pure firmware dma engine

#### rce\_stream/

Contains driver and application code for the RCE AXI stream DMA

#### Yocto/

Contains BitBake recipes for the aximemorymap and axistreamdma drivers.

<!--- ########################################################################################### -->

## Local CI Testing

The repository includes a local CI runner that validates the full Phase 3 test suite without requiring `sudo` on the host. It boots a QEMU virtual machine under TCG emulation (no KVM, no `/dev/kvm` access needed), loads the `datadev_emulator` and `datadev` kernel modules inside the VM, runs the test suite, and reports pass/fail. The same test scripts run in GitHub Actions CI (`.github/workflows/ci_pipeline.yml`), so local and CI behavior are identical.

> **Host kernel must match the guest cloud-image kernel.** `run_local_ci.sh` builds the `.ko` modules on the host against `linux-headers-$(uname -r)` and then `insmod`s them inside the Ubuntu 24.04 cloud-image VM. Kernel-module `vermagic` must match, so this flow only works when the host and guest run the same kernel series (Ubuntu 24.04 cloud images ship a `6.8.x` generic kernel). If the host kernel differs — for example an Ubuntu 22.04 host on `5.15`, a Rocky/RHEL 9 host, or any system whose `uname -r` disagrees with the cloud image — use the KVM-based local runner under [`scripts/ci-local/`](scripts/ci-local/) instead. That runner installs guest-matching headers inside the VM and builds there, eliminating the host/guest kernel dependency (see [`scripts/LOCAL_CI_TESTING.md`](scripts/LOCAL_CI_TESTING.md)).

### Prerequisites

Ubuntu / Debian:

```bash
sudo apt-get install qemu-system-x86 qemu-utils cloud-image-utils \
                     build-essential linux-headers-$(uname -r)
```

RHEL / CentOS / Fedora:

```bash
sudo dnf install qemu-kvm qemu-img genisoimage make gcc gcc-c++ kernel-devel
```

Required tools:

- `qemu-system-x86_64` — QEMU full-system emulator (TCG mode, no KVM needed)
- `qemu-img` — overlay image creation
- `cloud-localds` **or** `genisoimage` **or** `mkisofs` — builds the cloud-init seed ISO
- `curl` or `wget` — one-time cloud image download
- `make`, `gcc`, `g++` — build kernel modules and test binaries
- Linux kernel headers matching your host kernel

### Usage

From the repository root:

```bash
./run_local_ci.sh
```

This will:

1. Check prerequisites (exits with guidance if anything is missing)
2. Build the `nvidia_p2p_stub` module (loaded first to satisfy the emulator's `emu_gpu_register_drain_cb` symbol dependency), the emulator kernel module, the `datadev` driver, and all test binaries (`dmaLoopTest`, `dmaRate`, `dmaIoctlTest`, `dmaFileOpsTest`, `dmaErrorTest`)
3. Download the Ubuntu 24.04 cloud image (first run only, ~600 MB, cached at `~/.cache/aes-stream-local-ci/`)
4. Boot a QEMU VM with the project directory shared via 9p virtfs at `/mnt/host` in the guest
5. Inside the VM: `insmod` the three modules in order (`nvidia_p2p_stub` → `datadev_emulator` → `datadev`), run `tests/run_tests.sh` then `tests/test_params.sh`, then unload in reverse order
6. Capture the VM exit code and print overall PASS / FAIL

Exit code `0` means all tests passed. Non-zero means at least one test failed — see the VM console output for details.

### Environment Variables

Override the defaults by exporting these before running the script:

| Variable | Default | Purpose |
|----------|---------|---------|
| `VM_MEM` | `2G` | VM memory size |
| `VM_CPUS` | `2` | VM vCPU count |
| `VM_TIMEOUT` | `600` | QEMU wall-clock timeout in seconds |
| `CLOUD_IMG_URL` | Ubuntu 24.04 cloud image URL | Change distro/version |
| `CACHE_DIR` | `~/.cache/aes-stream-local-ci` | Where the base cloud image is cached |

### Troubleshooting

- **"qemu-system-x86_64 not found"** — install QEMU (see Prerequisites above)
- **"cloud-localds not found"** — install `cloud-image-utils` (Debian/Ubuntu) or `genisoimage` (RHEL/Fedora)
- **VM boot timeout** — TCG emulation is slow, especially on first cloud-init run (~1–2 min). Raise the limit with `VM_TIMEOUT=1200 ./run_local_ci.sh`
- **Tests fail in VM but pass in GitHub Actions** — TCG-emulated throughput is much lower than native, so timing-sensitive tests may behave differently. The test suite is designed to tolerate this; report any persistent failures.
- **"VM did not record exit code"** — usually a boot failure or a timeout before cloud-init `runcmd` finished. Check the serial console output printed to your terminal.

<!--- ########################################################################################### -->

## Continuous Integration

A single unified GitHub Actions workflow runs on every `push` event:

[![CI Pipeline](https://github.com/slaclab/aes-stream-drivers/actions/workflows/ci_pipeline.yml/badge.svg)](https://github.com/slaclab/aes-stream-drivers/actions/workflows/ci_pipeline.yml)

| Workflow | Purpose | Runner Environment |
|----------|---------|--------------------|
| [`ci_pipeline.yml`](.github/workflows/ci_pipeline.yml) — **CI Pipeline** | Unified CI combining repo integration and emulator/runtime validation: documentation + lint/static checks, multi-distro kernel-module build + load + test matrix (`ubuntu:22.04`, `ubuntu:24.04`, `rockylinux:9`, `debian:experimental`, `fedora:rawhide`) for both the CPU and GPU stacks, end-to-end Phase 3 and Phase 4 test coverage against the `datadev_emulator` + `nvidia_p2p_stub` pair, `dmesg` scanning for oops/panic/BUG, DKMS tarball smoke + full-install validation, and release packaging on tagged releases | `ubuntu-24.04` hosted runner; every matrix cell executes in a containerized distro image with host kernel headers bind-mounted. The CPU/GPU load + test steps are gated by `CI_HOST_MATCH=1` so they only fire on cells whose kernel matches the host runner (passwordless `sudo` + `CAP_SYS_MODULE` for `insmod`); other cells are compile-only. |

### Single unified workflow

`ci_pipeline.yml` replaces the previously separate `aes_ci.yml` (compile matrix) and `emu_ci.yml` (emulator runtime tests). One workflow means one badge, one summary, one place to look when something is red, and no possibility of the two drifting relative to each other. Broad compile coverage across distros, static analysis / lint checks, and runtime tests that require loading kernel modules now all live in the same pipeline.

Repo maintainers can still require specific job names from the unified workflow as branch-protection gates — for example `test_and_document`, `cpu_test (ubuntu:24.04)`, and `gpu_test (ubuntu:24.04)`.

### Runtime tests share code with the local VM runner

The workflow invokes the same `tests/run_tests.sh` + `tests/test_params.sh` scripts that `./run_local_ci.sh` runs inside a QEMU VM (see **Local CI Testing** above). Behaviour in CI and the local VM is therefore identical — if a test passes locally via `./run_local_ci.sh`, it should pass in CI, and vice versa (with the caveat that TCG emulation used locally is much slower than the hosted runners, so timing-sensitive assertions are intentionally tolerant).

### How to interpret a CI failure

When `ci_pipeline.yml` reports red on a push:

1. Open the failing run and click the **Summary** tab. Each test-suite step renders a PASS/FAIL count table; failing tests are listed in a fenced block.
2. Scroll the workflow view for red **`::error::` annotations** — each failing test emits one with the test name and exit code (e.g. `run_tests.sh: error_paths failed (exit=1)`).
3. Download the `cpu-ci-diag-*` or `gpu-ci-diag-*` artifact from the run's **Artifacts** panel. It contains `dmesg.txt`, the saved test-suite logs (`/tmp/phase3_tests.log`, `/tmp/phase4_tests.log`, `/tmp/test_params.log`), any `dma_loop_output*.txt`, and the built `.ko` modules — enough to reproduce a post-mortem without re-running CI.

<!--- ########################################################################################### -->

# How to build the data\_dev driver

```bash
# Go to the base directory
$ cd aes-stream-drivers

# Build the drivers
$ make driver

# Build the applications
$ make app
```

## How to load the data\_dev driver

```bash
# Go to the base directory
$ cd aes-stream-drivers

# Load the driver for the current kernel
$ sudo insmod install/$(uname -r)/datadev.ko
```

<!--- ########################################################################################### -->

# How to use the Yocto recipes

The Yocto recipes can be trivially included in your Yocto project via symlink.

```bash
$ ln -s $aes_stream_drivers/Yocto/recipes-kernel $myproject/sources/meta-user/recipes-kernel
```

Make sure to set the following variables in your local.conf:
```bash
# Substitute these values with your own desired settings
DMA_TX_BUFF_COUNT = 128
DMA_RX_BUFF_COUNT = 128
DMA_BUFF_SIZE     = 131072
```

For a practical example of how to integrate these recipes into a Yocto project, see [axi-soc-ultra-plus-core](https://github.com/slaclab/axi-soc-ultra-plus-core).

<!--- ########################################################################################### -->

# How to build the RCE drivers

```bash
# Go to the base directory
$ cd aes-stream-drivers

# Source the setup script (required for cross-compiling)
$ source /sdf/group/faders/tools/xilinx/2016.4/Vivado/2016.4/settings64.sh

# Build the drivers
$ make rce
```

<!--- ########################################################################################### -->
