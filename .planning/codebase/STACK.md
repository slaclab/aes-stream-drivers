# Technology Stack

**Analysis Date:** 2026-03-25

## Languages

**Primary:**
- C [Kernel space] - Used for all Linux kernel drivers (datadev, rce_stream, rce_memmap, rce_hp_buffers)
- C++ [User space] - Used for application code (dmaRead, dmaWrite, dmaLoopTest, dmaRate, test)

**Secondary:**
- Shell [Build scripts] - Makefiles and build/load scripts
- Python [Utilities] - run-clang-tidy.py, filter-clangdb.py, uploadTag.py
- BitBake [Yocto recipes] - Recipe files for Yocto build system

## Runtime

**Environment:**
- Linux Kernel - Driver runtime environment
- User space Linux - Application runtime environment

**Kernel Versions:**
- Support for multiple kernel versions via build system discovery
- Kernel version detection via `/lib/modules/*/build`

**Build System:**
- GNU Make - Primary build system
- Cross-compile support for ARM/RCE targets

## Frameworks

**Core:**
- Linux Kernel Driver Framework
  - Character device driver model
  - DMA subsystem (dma_alloc_coherent, dma_free_coherent)
  - PCI/PCIe subsystem (for PCIe cards)
  - Platform driver framework (for RCE devices)
  - Memory mapping (ioremap, iounmap)

**Testing:**
- PRBS (Pseudo-Random Binary Sequence) - `common/app_lib/PrbsData.cpp` for data integrity testing
- DMA loopback testing - `common/app/dmaLoopTest.cpp`

**Build/Dev:**
- Make - Build system with targets: `app`, `driver`, `rce`
- clang-tidy - Static analysis with custom config (`.clang-tidy`)
- clangd - Language server support (`.clangd`)

## Key Dependencies

**Critical:**
- Linux Kernel Headers - Required for all kernel modules
- DMA subsystem APIs - Core driver functionality

**GPU Support:**
- NVIDIA kernel drivers - For GPUDirect RDMA support
  - `nv-p2p.h` - NVIDIA Peer-to-Peer API
  - `nvidia_p2p_get_pages()`, `nvidia_p2p_dma_map_pages()` - Memory pinning and mapping

**Infrastructure:**
-.argp - Argument parsing in user-space applications
- `linux/module.h` - Kernel module infrastructure
- `linux/dma-mapping.h` - DMA buffer management

**Yocto Dependencies:**
- bitbake - Build tool
- OpenEmbedded class `module.bbclass` - Kernel module packaging

## Configuration

**Environment:**
- Kernel version discovery from `/lib/modules/*/build`
- Architecture detection via `uname -m`
- Cross-compile toolchain for ARM/RCE targets
- Xilinx Vivado tools for RCE builds (`/sdf/group/faders/tools/xilinx/`)

**Build Config:**
- `KVER` - Target kernel version
- `ARCH` - Target architecture (arm, x86_64)
- `CROSS_COMPILE` - Cross-compiler prefix (e.g., `arm-xilinx-linux-gnueabi-`)
- `NVIDIA_DRIVERS` - Path to NVIDIA driver source tree

**Key configs required:**
- DMA buffer counts (TX/RX) - Configurable at module load time
- Buffer size configuration
- Buffer mode (coherent, streaming, ARM ACP)
- IRQ holdoff and timeout settings

## Platform Requirements

**Development:**
- Linux host with kernel headers
- GCC compiler
- Make build system
- For ARM/RCE: Xilinx cross-compiler toolchain

**Production:**
- Target platforms:
  - x86_64 servers with PCIe cards (TID-AIR, GPCC, etc.)
  - ARM64 RCE (Remote Computer Element) platforms
  - NVIDIA GPU systems (for GPUDirect RDMA)
- Linux kernel 3.10+ (with kernel version-specific compatibility checks)

**Hardware Targets:**
- TID-AIR generic DAQ PCIe cards
- Xilinx-based PCIe cards (Alveo U50, U200, U250, U280, U55C, etc.)
- KCU105, KC705, VCU128, Varium C1100
- Abaco PC821 Ku085, Ku115
- BittWare XUP-VV8, XUP-VV8U9P, XUP-VV8U13P
- RCE platforms with AXI stream DMA hardware

---

*Stack analysis: 2026-03-25*