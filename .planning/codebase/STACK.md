# Technology Stack

**Analysis Date:** 2026-03-26

## Languages

**Primary:**
- C (C99/C11) - All Linux kernel driver code: `common/driver/`, `rce_stream/driver/src/`, `rce_memmap/driver/src/`, `rce_hp_buffers/driver/src/`, `Yocto/recipes-kernel/*/files/`
- C++ (C++11) - All userspace application code: `data_dev/app/src/`, `rce_stream/app/`, `common/app/`, `common/app_lib/`

**Secondary:**
- CUDA (`.cu`) - GPU test application: `data_dev/app/src/rdmaTest.cu`
- Python 3.12 - Utility scripts: `scripts/run-clang-tidy.py`, `scripts/filter-clangdb.py`, `scripts/uploadTag.py`
- Bash - Build and load scripts: `data_dev/driver/comp_and_load_drivers.sh`, `data_dev/driver/build-nvidia.sh`, `data_dev/scripts/load_datadev.sh`
- BitBake - Yocto recipes: `Yocto/recipes-kernel/`, `Yocto/recipes-tests/`

## Runtime

**Environment:**
- Linux kernel space - All driver modules (`.ko` files)
- Linux userspace - Application binaries

**Kernel Version Support:**
- Minimum: 3.10 (RHEL7/CentOS7; `KERNEL_VERSION(3, 10, 0)` compatibility guards present)
- Tested range: 3.10, 5.14 (RHEL9 frankenstein), 5.15, 5.19, 6.5, 6.8, plus latest Debian experimental
- RHEL/Rocky Linux 9 backport handling: `RHEL_RELEASE_VERSION` macros for 9.3/9.4 backports
- Key compatibility boundaries in `common/driver/dma_common.c`:
  - `< 4.16`: custom `__poll_t` typedef
  - `>= 5.6.0`: `proc_ops` struct (vs legacy `file_operations` for procfs)
  - `>= 6.4.0` / RHEL 9.4: `class_create()` signature change

**Package Manager:**
- None (kernel module; no package manager for driver itself)
- Python pip for CI tooling: `pip_requirements.txt` (`pygithub`, `cpplint`)

## Frameworks

**Core:**
- Linux Kernel Module Framework
  - `linux/module.h`, `linux/moduleparam.h` - Module lifecycle and parameter handling
  - Character device model - `cdev_init()`, `cdev_add()`, `alloc_chrdev_region()`
  - PCI subsystem - `pci_register_driver()`, `pci_enable_device()`, `pci_request_regions()`
  - DMA mapping API - `dma_alloc_coherent()`, `dma_map_single()`, kernel 5.15+ `dma_alloc_pages()`
  - Interrupt handling - `request_irq()`, `free_irq()`
  - Workqueue - `linux/workqueue.h`, `struct work_struct`, `struct delayed_work`
  - Wait queue - `wait_queue_head_t` for blocking read/poll
  - Spinlock - `spinlock_t` for IRQ-safe buffer queue operations
  - Proc filesystem - `linux/proc_fs.h`, `linux/seq_file.h` for `/proc/datadev_*`
  - Memory-mapped I/O - `ioremap()`, `iounmap()`, `request_mem_region()`

**Testing:**
- PRBS (Pseudo-Random Binary Sequence) data integrity: `common/app_lib/PrbsData.cpp`, `common/app_lib/PrbsData.h`
- DMA loopback: `common/app/dmaLoopTest.cpp`
- RDMA GPU test: `data_dev/app/src/rdmaTest.cu` (requires CUDA)

**Build/Dev:**
- GNU Make - Primary build system; targets: `app`, `driver`, `rce`
- DKMS - Dynamic Kernel Module Support: `data_dev/driver/dkms.conf`, `data_dev/driver/dkms-gpu.conf`
- clang-tidy - Static analysis: `.clang-tidy` (`bugprone-*`, `performance-*`)
- clangd - Language server: `.clangd`
- cpplint - C/C++ style linter: `CPPLINT.cfg` (line length 250, several checks disabled for kernel code)
- bear - Compilation database generator (used with `bear -- make driver app` in CI)
- sparse - Kernel static checker (`C=2 CF=-Wsparse-error`, Debian experimental only)

## Key Dependencies

**Critical:**
- Linux kernel headers - Required for all kernel modules; resolved from `/lib/modules/$(KVER)/build`
- `nv-p2p.h` (NVIDIA P2P API) - Required for GPU build; found under `/usr/src/nvidia-*` or `/usr/src/nvidia-open-*`

**GPU/RDMA (optional, enabled by `NVIDIA_DRIVERS` build variable):**
- NVIDIA open kernel driver source tree - Provides `nv-p2p.h`, `nvidia_p2p_get_pages()`, `nvidia_p2p_dma_map_pages()`, `nvidia_p2p_dma_unmap_pages()`
- Module.symvers from NVIDIA driver - Linked via `KBUILD_EXTRA_SYMBOLS`

**Userspace Applications:**
- `libpthread` - Linked via `-lpthread` in `data_dev/app/Makefile`
- CUDA toolkit (`nvcc` at `/usr/local/cuda/bin/nvcc`) - For `rdmaTest.cu` only
- CUDA compute capability: `--gpu-architecture=compute_100` in `data_dev/app/Makefile`

**CI Python:**
- `pygithub` - Tag/release upload: `scripts/uploadTag.py`
- `cpplint` - C/C++ linting in CI pipeline

## Configuration

**Build-Time:**
- `KVER` - Target kernel version (defaults to `uname -r`)
- `ARCH` - Target architecture (defaults to `uname -m`; `arm` for RCE cross-builds)
- `CROSS_COMPILE` - Cross-compiler prefix (e.g., `arm-xilinx-linux-gnueabi-` for RCE)
- `NVIDIA_DRIVERS` - Path to NVIDIA driver source; enables `DATA_GPU=1` and `gpu_async.o` compilation
- `GITV` - Version string from `git describe --tags`; embedded as `GITV` in driver
- Compile flags: `-DDMA_IN_KERNEL=1 -DPCIE_DMA=1` always set for `datadev`

**Module Load-Time Parameters (`/etc/modprobe.d/datadev.conf`):**
- `cfgTxCount` - TX buffer count (default: 1024)
- `cfgRxCount` - RX buffer count (default: 1024)
- `cfgSize` - Buffer size in bytes (default: `0x20000` = 131072)
- `cfgMode` - Buffer mode: `BUFF_COHERENT=0x1`, `BUFF_STREAM=0x2`, `BUFF_ARM_ACP=0x4`
- `cfgCont` - Continuous mode (default: 1)
- `cfgIrqHold` - IRQ holdoff cycles (default: 10000)
- `cfgIrqDis` - Disable IRQ (default: 0)
- `cfgBgThold0`-`cfgBgThold7` - Background threshold array (default: all 0)
- `cfgTimeout` - DMA timeout value (default: `0xFFFF`)
- `cfgDevName` - Device name index (default: 0)
- `cfgDebug` - Debug logging (default: 0)

## Platform Requirements

**Development:**
- Linux host with kernel headers installed
- GCC (version matching kernel build; detected from `/proc/version` in `comp_and_load_drivers.sh`)
- GNU Make
- For RCE/ARM: Xilinx cross-compiler toolchain (`arm-xilinx-linux-gnueabi-`) and Vivado 2016.4

**Production Targets:**
- x86_64 Linux servers with SLAC PCIe cards (PCI vendor `0x1a4a`, device `0x2030`)
- NVIDIA GPU servers with GPUDirect RDMA (optional)
- ARM RCE platforms (Xilinx Zynq-based; kernel `linux-xlnx-v2016.4`)
- Yocto-based embedded Linux (AXI SoC Ultra+ platforms)

**Supported Distros (CI-verified):**
- Ubuntu 22.04 (kernels 5.19, 6.5, 6.8)
- Ubuntu 24.04 (kernel 6.8)
- Debian experimental (latest kernel)
- Rocky Linux 9 (kernel 5.14)
- CentOS 7 (kernel 3.10; via `ghcr.io/jjl772/centos7-vault`)

---

*Stack analysis: 2026-03-26*
