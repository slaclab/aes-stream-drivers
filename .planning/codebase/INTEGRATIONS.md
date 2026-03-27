# External Integrations

**Analysis Date:** 2026-03-26

## Hardware Interfaces

**PCIe (PCI Express):**
- Primary bus interface for all `datadev` PCIe cards
- PCI vendor ID: `0x1a4a` (SLAC), device ID: `0x2030`
- Driver registration: `pci_register_driver()` / `pci_unregister_driver()` in `common/driver/data_dev_top.c`
- BAR mapping: `pci_request_regions()` + `ioremap()` for MMIO register access
- MSI interrupt support: `pci_enable_msi()` via `Dma_Init()` in `common/driver/dma_common.c`
- DMA: `pci_set_dma_mask()`, `pci_set_consistent_dma_mask()` for 64-bit DMA
- Max simultaneous devices: 32 (`MAX_DMA_DEVICES` in `common/driver/data_dev_top.h`)
- Device register map (offsets from BAR0):
  - `0x00000000` - AXI Gen2 DMA engine (`AGEN2_OFF`, size `0x10000`)
  - `0x00010000` - PCIe PHY (`PHY_OFF`, size `0x10000`)
  - `0x00020000` - AXI Version register block (`AVER_OFF`, size `0x10000`)
  - `0x00030000` - PROM (`PROM_OFF`, size `0x50000`)
  - `0x00800000` - User register space (`USER_OFF`, size `0x800000`)

**AXI Stream DMA Engine (FPGA-side):**
- Implemented as Gen1 and Gen2 variants
- Gen1: 64-bit descriptor ring, `common/driver/axis_gen1.c` / `axis_gen1.h`
- Gen2: 64-bit and 128-bit descriptor ring, `common/driver/axis_gen2.c` / `axis_gen2.h`
- Register map: `struct AxisG2Reg` in `common/driver/axis_gen2.h`
  - Control registers: `enableVer`, `intEnable`, `contEnable`, `dropEnable`
  - Address registers: `wrBaseAddrLow/High`, `rdBaseAddrLow/High`
  - FIFO registers: `readFifoA-D`, `writeFifoA/B`
  - IRQ: `intAckAndEnable`, `irqHoldOff`, `forceInt`
  - DMA descriptor ring: `dmaAddr[4096]` at offset `0x4000`
- Workqueue-based deferred IRQ processing: `AxisG2_WqTask_IrqForce`, `AxisG2_WqTask_Poll`, `AxisG2_WqTask_Service`
- Background threshold support: `bgThold[8]`, `bgCount[8]` registers

**AXI Version Register Block (FPGA firmware versioning):**
- Structure: `struct AxiVersion_Reg` in `common/driver/axi_version.h`
- Provides: `firmwareVersion`, `gitHash[40]`, `buildString[64]`, `deviceId`, `dnaValue[4]`
- Exposed to userspace via `AxiVersion_Get()` ioctl handler
- User hardware type at `userValues[0x9]` offset

**ARM AXI / RCE Platform:**
- Xilinx Zynq-based RCE (Remote Computer Element) platforms
- Driver: `rce_stream/driver/src/rce_top.c`
- ARM Coherent Port (ACP) mode: `BUFF_ARM_ACP=0x4`, uses `arm_coherent_dma_ops`
- Cross-compile target: `arm-xilinx-linux-gnueabi-` toolchain
- Kernel source: `linux-xlnx-v2016.4` at `/sdf/group/faders/tools/xilinx/rce_linux_kernel/`

**AXI Memory Map (RCE register access):**
- Driver: `rce_memmap/driver/src/rce_map.c`
- Provides raw register read/write to AXI address space from userspace

**RCE HP Buffers:**
- Driver: `rce_hp_buffers/driver/src/rce_hp.c`
- Allocates physically-contiguous memory blocks for firmware-side DMA engines

## NVIDIA GPUDirect RDMA (Optional Integration)

**API:** NVIDIA Peer-to-Peer (P2P) kernel API (`nv-p2p.h`)

**Activation:** Build with `NVIDIA_DRIVERS=/path/to/nvidia-src`; adds `-DDATA_GPU=1` and compiles `common/driver/gpu_async.c`

**Key NVIDIA functions used** (in `common/driver/gpu_async.c`):
- `nvidia_p2p_get_pages()` - Pin GPU memory pages and obtain `nvidia_p2p_page_table_t`
- `nvidia_p2p_dma_map_pages()` - Map GPU pages for DMA, obtain `nvidia_p2p_dma_mapping`
- `nvidia_p2p_dma_unmap_pages()` - Unmap GPU DMA pages
- `nvidia_p2p_free_page_table()` - Release pinned GPU pages
- Callback `Gpu_FreeNvidia()` - Registered as free callback for implicit page table invalidation

**GPU buffer constraints:**
- Alignment: 64KB boundary (`GPU_BOUND_SIZE = 1 << 16`)
- Max buffers: 1024 per device (`MAX_GPU_BUFFERS` in `common/driver/gpu_async.h`)
- Firmware version check: GpuAsyncCore versions 1-4 supported (`DATAGPU_MAX_VERSION=4`)

**GpuAsyncCore register block** (firmware IP):
- Version register: read via `readGpuAsyncReg()` in `include/GpuAsyncRegs.h`
- Definitions: `include/GpuAsync.h`, `include/GpuAsyncRegs.h`

**NVIDIA module load order** (`data_dev/driver/comp_and_load_drivers.sh`):
1. `nvidia.ko` (with `NVreg_EnableStreamMemOPs=1`)
2. `nvidia-modeset.ko`
3. `nvidia-drm.ko`
4. `nvidia-uvm.ko`
5. `datadev.ko` (with `NVIDIA_DRIVERS` set)

## Userspace Interfaces

**Character Device:**
- Device nodes: `/dev/datadev_0` through `/dev/datadev_31`
- Permissions set via `Dma_DevNode()` udev callback (mode `0666`)
- Created via `alloc_chrdev_region()` + `cdev_add()` + `device_create()`

**File Operations** (defined in `common/driver/dma_common.c`):
- `open` / `release` - Per-descriptor allocation (`struct DmaDesc`)
- `read` - Receive a completed DMA buffer; blocks via `wait_queue`; returns `struct DmaReadData`
- `write` - Submit a DMA buffer for transmission; accepts `struct DmaWriteData`
- `poll` - `select()`/`poll()` support via `dmaQueuePoll()`
- `fasync` - Async signal notification (`SIGIO`) via `fasync_helper()`
- `mmap` - Zero-copy buffer sharing: maps DMA buffers into userspace VA
- `unlocked_ioctl` / `compat_ioctl` - All control via `Dma_Ioctl()`

**ioctl Command Set** (defined in `include/DmaDriver.h`, API version `0x06`):
- `DMA_Get_Buff_Count 0x1001` - Total buffer count
- `DMA_Get_Buff_Size 0x1002` - Buffer size in bytes
- `DMA_Set_Debug 0x1003` - Enable/disable debug logging
- `DMA_Set_Mask 0x1004` - Set destination channel mask (32-bit)
- `DMA_Ret_Index 0x1005` - Return buffer index to driver
- `DMA_Get_Index 0x1006` - Get next available buffer index
- `DMA_Read_Ready 0x1007` - Check if data is ready to read
- `DMA_Set_MaskBytes 0x1008` - Set destination mask (512-byte bitmask)
- `DMA_Get_Version 0x1009` - Get driver API version
- `DMA_Write_Register 0x100A` - Write hardware register (`struct DmaRegisterData`)
- `DMA_Read_Register 0x100B` - Read hardware register
- `DMA_Get_RxBuff_Count 0x100C` / `DMA_Get_TxBuff_Count 0x100D` - Buffer pool sizes
- `DMA_Get_TxBuffinUser_Count 0x100F` through `DMA_Get_RxBuffMiss_Count 0x1018` - Buffer state counters
- `DMA_Get_GITV 0x1019` - Get firmware git version string
- `AXIS_Read_Ack 0x2001` - Acknowledge completed AXI Stream read (in `include/AxisDriver.h`)
- `AXIS_Write_ReqMissed 0x2002` - Signal missed write request
- `GPU_Add_Nvidia_Memory 0x8002` - Register GPU memory region for DMA
- `GPU_Rem_Nvidia_Memory 0x8003` - Unregister GPU memory region
- `GPU_Set_Write_Enable 0x8004` - Enable write on a GPU DMA buffer
- `GPU_Is_Gpu_Async_Supp 0x8005` - Check GPUDirect firmware support
- `GPU_Get_Gpu_Async_Ver 0x8006` - Get GpuAsyncCore firmware version
- `GPU_Get_Max_Buffers 0x8007` - Get max GPU DMA buffer count

**mmap Interface:**
- DMA buffers mapped to userspace for zero-copy access
- Buffer address lookup in `Dma_Mmap()` via `dmaFindBuffer()`
- `remap_pfn_range()` used to map physical DMA pages into userspace VMA

**Proc Filesystem:**
- Entry: `/proc/datadev_*` per device
- Exposes: buffer statistics, firmware version, hardware register state, GPU info
- Implementation: `seq_file` API; `Dma_SeqShow()` dispatches to hardware-specific `seqShow`
- Kernel 5.6+: uses `proc_ops`; older: uses `file_operations`

**DMA Buffer Destination Masking:**
- Each open file descriptor gets its own `struct DmaDesc` with a `destMask[512]` bitmask
- `DMA_MASK_SIZE=512` bytes = 4096 destination channels (`DMA_MAX_DEST = 8 * 512 * 8`)
- Inbound frames are routed to descriptors matching their destination channel

**Userspace Library** (`include/DmaDriver.h`, `include/AxisDriver.h`, `include/GpuAsync.h`):
- Inline C helper functions for use when `DMA_IN_KERNEL` is not defined
- `dmaWrite()`, `dmaWriteIndex()`, `dmaWriteVector()` - submit frames
- `dmaRead()`, `dmaReadIndex()` - receive frames
- `dmaGetIndex()`, `dmaRetIndex()` - zero-copy buffer management
- `dmaInitMasterMap()`, `dmaMapRegister()`, `dmaUnMapRegister()` - direct register access
- `gpuAddNvidiaMemory()`, `gpuRemNvidiaMemory()`, `gpuSetWriteEn()` - GPU control
- `axisSetFlags()`, `axisGetFuser()`, `axisGetLuser()`, `axisGetCont()` - AXIS frame flag helpers

## Build and CI Integrations

**GitHub Actions** (`.github/workflows/aes_ci.yml`):
- Trigger: every push
- Jobs:
  1. `test_and_document` (ubuntu-24.04): trailing whitespace/tab check, cpplint on all `.c`/`.cpp`/`.h`
  2. `build` (matrix): compile kernel module + userspace app across 5 distro containers; clang-tidy + sparse on Debian experimental
  3. `gen_release`: calls `slaclab/ruckus/.github/workflows/gen_release.yml@main` for tag-based releases
  4. `generate_dkms`: builds DKMS tarball and uploads to GitHub release assets (tag builds only)
- Container matrix: `ubuntu:22.04`, `ubuntu:24.04`, `rockylinux:9`, `debian:experimental`, `ghcr.io/jjl772/centos7-vault`
- CI secrets: `GH_TOKEN` for release uploads

**DKMS Integration:**
- Config: `data_dev/driver/dkms.conf`, `data_dev/driver/dkms-gpu.conf`
- Package name: `datadev-dkms`
- Install location: `/kernel/modules/misc`
- Auto-install on kernel upgrade: `AUTOINSTALL=yes`
- Installer script generated at CI time: `dkms add`, `dkms build`, `dkms install`

**Yocto/BitBake:**
- Recipes: `Yocto/recipes-kernel/aximemorymap/`, `Yocto/recipes-kernel/axistreamdma/`
- Test samples: `Yocto/recipes-tests/axidmasamples/`
- Uses `module.bbclass` for kernel module packaging
- Configurable via `local.conf`: `DMA_TX_BUFF_COUNT`, `DMA_RX_BUFF_COUNT`, `DMA_BUFF_SIZE`

**GitHub Release Automation:**
- Script: `scripts/uploadTag.py` (uses `pygithub`)
- Triggered from CI on tagged commits
- Uploads DKMS tarball as release asset

**External Kernel Source (RCE):**
- Xilinx RCE Linux kernel: `/sdf/group/faders/tools/xilinx/rce_linux_kernel/linux-xlnx-v2016.4`
- Backup: `linux-xlnx-v2016.1.01`
- Xilinx Vivado 2016.4 toolchain: `/sdf/group/faders/tools/xilinx/2016.4/Vivado/`

## Data Storage

**DMA Memory:**
- Coherent: `dma_alloc_coherent()` / `dma_free_coherent()` - CPU-accessible, cache-coherent
- Streaming: `dma_map_single()` / `dma_unmap_single()` - streaming DMA with explicit sync
- ARM ACP: `kzalloc()` + `virt_to_phys()` with `arm_coherent_dma_ops`
- Kernel 5.15+: `dma_alloc_pages()` / `dma_free_pages()` used in `common/driver/dma_buffer.c`
- Max buffers per list: 100,000 (`BUFFERS_PER_LIST` in `common/driver/dma_buffer.h`)

**Kernel Objects:**
- Built `.ko` files copied to `install/$(KVER)/` by top-level `make driver`

## Monitoring and Observability

**Kernel Log:**
- All messages via `pr_info()`, `pr_err()`, `dev_info()`, `dev_warn()`, `dev_err()`
- Module name prefix: `"datadev"` in all log lines
- Debug mode: `cfgDebug=1` module parameter enables verbose per-operation logging

**Proc Interface:**
- `/proc/datadev_*` - Per-device statistics
- Content includes: TX/RX buffer counts, buffer state (inHW, inQ, userHas), firmware version, IRQ count, GPU state

## Authentication and Access Control

- No external auth provider
- Device file permissions: `0666` set by `Dma_DevNode()` udev helper
- Device nodes: `/dev/datadev_0` through `/dev/datadev_N`
- Each `open()` creates an isolated `DmaDesc` context; descriptors own their destination mask

---

*Integration audit: 2026-03-26*
