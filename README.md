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

The repository includes a local CI runner that validates the full Phase 3 test suite without requiring `sudo` on the host. It boots a QEMU virtual machine under TCG emulation (no KVM, no `/dev/kvm` access needed), loads the `datadev_emulator` and `datadev` kernel modules inside the VM, runs the test suite, and reports pass/fail. The same test scripts run in GitHub Actions CI (`.github/workflows/emu_ci.yml`), so local and CI behavior are identical.

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
2. Build the emulator kernel module, the `datadev` driver, and all test binaries (`dmaLoopTest`, `dmaRate`, `dmaIoctlTest`, `dmaFileOpsTest`, `dmaErrorTest`)
3. Download the Ubuntu 24.04 cloud image (first run only, ~600 MB, cached at `~/.cache/aes-stream-local-ci/`)
4. Boot a QEMU VM with the project directory shared via 9p virtfs at `/mnt/host` in the guest
5. Inside the VM: `insmod` both modules, run `tests/run_tests.sh` then `tests/test_params.sh`, then unload modules
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

Two GitHub Actions workflows run on every `push` event:

[![Repo Integration](https://github.com/slaclab/aes-stream-drivers/actions/workflows/aes_ci.yml/badge.svg)](https://github.com/slaclab/aes-stream-drivers/actions/workflows/aes_ci.yml)
[![Emulator CI](https://github.com/slaclab/aes-stream-drivers/actions/workflows/emu_ci.yml/badge.svg)](https://github.com/slaclab/aes-stream-drivers/actions/workflows/emu_ci.yml)

| Workflow | Purpose | Runner Environment |
|----------|---------|--------------------|
| [`aes_ci.yml`](.github/workflows/aes_ci.yml) — **Repo Integration** | Multi-distro kernel-module **compile matrix** (Ubuntu 22.04/24.04, Rocky Linux 9, Debian experimental, CentOS 7), `cpplint`, trailing-whitespace / tab check, `clang-tidy` + `sparse` on Debian experimental, DKMS tarball packaging on tagged releases | Containerized distro images on an `ubuntu-24.04` host |
| [`emu_ci.yml`](.github/workflows/emu_ci.yml) — **Emulator CI** | End-to-end **runtime tests** — build `datadev_emulator.ko`, `nvidia_p2p_stub.ko`, `datadev.ko`, and user-space test apps; `insmod`; run the full Phase 3 and Phase 4 test suite (ioctl / file-ops / error-paths / multi-channel / `/proc` / DMA rate / module parameters / GPU ioctls); scan `dmesg` for oops/panic/BUG; execute three load/unload cycles | `ubuntu-22.04` and `ubuntu-24.04` hosted VMs (needs passwordless `sudo` for `insmod`, which containers cannot provide) |

### Why two workflows?

`aes_ci.yml` runs its compile-matrix jobs inside container images (`rockylinux:9`, `debian:experimental`, `ghcr.io/jjl772/centos7-vault`, etc.) to verify the driver still compiles across distros with older kernels. These containers do not provide passwordless `sudo` and cannot load kernel modules. `emu_ci.yml` must therefore run on non-container hosted VMs where `sudo insmod` works. Keeping the two concerns in separate files keeps each workflow focused and keeps failures in one from blocking the other.

The two workflows are **independent** — a failure in one does not block the other. Both must be green for a push to be considered passing. Repo maintainers can require specific job names as branch-protection gates (e.g. `Test And Generate Documentation`, `Compile Kernel Module (ubuntu:24.04)`, `Build & DMA Test (ubuntu-24.04)`, `Build & GPU Test (ubuntu-24.04)`).

### Runtime tests share code with the local VM runner

`emu_ci.yml` invokes the same `tests/run_tests.sh` + `tests/test_params.sh` scripts that `./run_local_ci.sh` runs inside a QEMU VM (see **Local CI Testing** above). Behaviour in CI and the local VM is therefore identical — if a test passes locally via `./run_local_ci.sh`, it should pass in CI, and vice versa (with the caveat that TCG emulation used locally is much slower than the hosted runners, so timing-sensitive assertions are intentionally tolerant).

### How to interpret a CI failure

When `emu_ci.yml` reports red on a push:

1. Open the failing run and click the **Summary** tab. Each test-suite step renders a PASS/FAIL count table; failing tests are listed in a fenced block.
2. Scroll the workflow view for red **`::error::` annotations** — each failing test emits one with the test name and exit code (e.g. `run_tests.sh: error_paths failed (exit=1)`).
3. Download the `emu-ci-diag-<job>-<os>` artifact from the run's **Artifacts** panel. It contains `dmesg.txt`, the saved test-suite logs (`/tmp/phase3_tests.log`, `/tmp/phase4_tests.log`, `/tmp/test_params.log`), any `dma_loop_output*.txt`, and the built `.ko` modules — enough to reproduce a post-mortem without re-running CI.

If `aes_ci.yml` reports red, the failure is a compile, lint, or static-analysis issue — check the specific job's log.

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
