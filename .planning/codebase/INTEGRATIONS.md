# System Integrations

**Analysis Date:** 2026-03-25

## APIs & External Services

**Hardware Interfaces:**
- PCIe (PCI Express) - Primary bus interface for PCIe cards
  - Used by: `datadev`, `axi_version`, `axis_gen1`, `axis_gen2`
  - Connection: Direct PCIe BAR mapping
  - Driver: `rce_stream`, `rce_memmap`

**Xilinx AXI (Advanced eXtensible Interface):**
- AXI Stream - High-speed streaming interface
  - SDK/Client: `axi_stream_dma` kernel module
  - Registers: `rce_stream/driver/src/rce_top.c`
- AXI Memory Map - Memory-mapped register access
  - SDK/Client: `rce_memmap` kernel module
  - Registers: `rce_memmap/driver/src/rce_map.c`

## Data Storage

**Databases:**
- None - No database integration

**File Storage:**
- Local filesystem - Driver modules (`.ko` files)
  - Location: `install/` directory
  - Format: Kernel object files

**Temporary Storage:**
- DMA buffers - Coherent and streaming memory regions
  - Allocated via `dma_alloc_coherent()` and `dma_free_coherent()`
  - Used for high-speed data transfer between FPGA and CPU

**Caching:**
- None - No caching layer

## Authentication & Identity

**Auth Provider:**
- Custom - No external authentication
- Device file permissions via `chmod 0666` on `/dev/datadev_*`

**Device Access:**
- Character device interface
- Device nodes: `/dev/datadev_0`, `/dev/axi_stream_dma_*`
- Open via standard `open(2)` system call

## Monitoring & Observability

**Error Tracking:**
- Kernel logging - `printk()` and `dev_*()` macros
- `/proc` filesystem - Device statistics via `/proc/datadev_*`

**Logs:**
- Kernel log buffer - All driver messages via `dev_info()`, `dev_warn()`, `dev_err()`
- Proc interface - `/proc/datadev_*` provides DMA buffer statistics

**Debug Interface:**
- `/proc/datadev_*` - Detailed driver state including:
  - Buffer counts (RX/TX)
  - Buffer usage statistics
  - Hardware register states
  - Version information

## CI/CD & Deployment

**Hosting:**
- Not applicable - This is a driver repository, not a deployed service

**CI Pipeline:**
- GitHub Actions - `.github/workflows/aes_ci.yml`

**Yocto Integration:**
- BitBake recipes for kernel modules:
  - `aximemorymap` - Memory map driver
  - `axistreamdma` - AXI stream DMA driver
- Recipes location: `Yocto/recipes-kernel/`

**Deployment Methods:**
- Direct deployment: `insmod` / `rmmod`
- Yocto: Package as kernel module via BitBake
- Scripted: `comp_and_load_drivers.sh`, `load_datadev.sh`

## Environment Configuration

**Required env vars:**
- None - Configuration via module parameters

**Module Parameters:**
- `cfgTxCount` - TX buffer count (default: 128)
- `cfgRxCount` - RX buffer count (default: 128)
- `cfgSize` - Buffer size in bytes (default: 131072)
- `cfgMode` - Buffer mode (default: 1, coherency flags)
- `cfgCont` - Continuous mode (default: 1)
- `cfgBgThold` - Background thresholds (array of 8)

**Kernel Parameters:**
- None - All configuration via module parameters

## Webhooks & Callbacks

**Incoming:**
- None - No webhook endpoints

**Outgoing:**
- None - No outbound webhook calls

## Hardware-Specific Integrations

**FPGA/ASIC:**
- AXI Stream DMA engine - Xilinx IP core
  - Version 1 and Version 2 protocol support
  - 64-bit and 128-bit descriptor support
  - Hardware registers mapped via PCIe/AXI

**GPU Direct:**
- NVIDIA GPUDirect RDMA
  - Integration: `data_dev/driver/src/gpu_async.c`
  - SDK/Client: `nv-p2p.h` NVIDIA P2P API
  - Memory pinning: `nvidia_p2p_get_pages()`
  - DMA mapping: `nvidia_p2p_dma_map_pages()`
  - Memory unmap: `nvidia_p2p_dma_unmap_pages()`

**ARM RCE:**
- Xilinx Zynq/versal RCE platforms
- ARM Coherent Port (ACP) support
- Kernel: `linux-xlnx-v2016.4` and `backup/linux-xlnx-v2016.1.01`

## Protocol Implementations

**PCIe:**
- Configuration space access
- BAR mapping for register and memory regions
- MSI/MSI-X interrupt support
- DMA transaction completion handling

**AXI Stream:**
- Gen1 protocol - 64-bit descriptors
- Gen2 protocol - 64-bit and 128-bit descriptors
- Ring buffer management for descriptors
- Interrupt coalescing and holdoff

**ARM ACP (AXI Coherent Port):**
- Cache-coherent memory access
- `BUFF_ARM_ACP` mode in buffer allocation
- `set_dma_ops(&pdev->dev, &arm_coherent_dma_ops)`

## Memory Management

**DMA Memory Types:**
- Coherent memory - `dma_alloc_coherent()` / `dma_free_coherent()`
- Streaming memory - `dma_map_single()` / `dma_unmap_single()`
- ARM ACP memory - `kzalloc()` with `virt_to_phys()`
- Page-based (kernel 5.15+) - `dma_alloc_pages()` / `dma_free_pages()`

**GPU Memory Integration:**
- User-space GPU memory mapping
- 64KB page alignment required
- Memory registration via `nvidia_p2p_get_pages()`
- DMA address translation via `nvidia_p2p_dma_map_pages()`

## Build Toolchain Integrations

**Xilinx Tools:**
- Vivado 2016.4+ - FPGA bitstream generation
- RCE Linux kernel source - `linux-xlnx-v2016.4`

**GCC Toolchains:**
- x86_64: Standard system GCC
- ARM: `arm-xilinx-linux-gnueabi-` (Xilinx toolchain)

**Static Analysis:**
- clang-tidy - `.clang-tidy` configuration
- cpplint - `.clangd` and `CPPLINT.cfg` configuration

---

*Integration audit: 2026-03-25*