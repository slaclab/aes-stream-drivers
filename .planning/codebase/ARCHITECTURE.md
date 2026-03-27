# Architecture

**Analysis Date:** 2026-03-26

## System Overview

```
  User Space Application
  ┌───────────────────────────────────────────────────────────┐
  │  open("/dev/datadev_0")  read()  write()  ioctl()  mmap() │
  │  DmaDriver.h / AxisDriver.h / DataDriver.h  (inline API)  │
  └────────────────────┬──────────────────────────────────────┘
                       │  character device file operations
  ─────────────────────┼─────────────────────────────── kernel boundary
                       │
  Kernel: Common DMA Layer  (common/driver/)
  ┌───────────────────────────────────────────────────────────┐
  │  dma_common.c  – file_operations, Dma_Init/Clean,         │
  │                  Dma_Read/Write/Ioctl/Poll/Mmap            │
  │  dma_buffer.c  – DmaBuffer / DmaBufferList / DmaQueue      │
  │  axi_version.c – AxiVersion register read/show            │
  │  gpu_async.c   – NVIDIA GPUDirect peer-to-peer (optional)  │
  └──────────┬──────────────┬────────────────────────────────┘
             │ hwFunc vtable│ (struct hardware_functions)
       ┌─────┴───┐    ┌─────┴────┐
       │ axis_   │    │ axis_    │       (common/driver/)
       │ gen2.c  │    │ gen1.c   │
       │ AxisG2  │    │ AxisG1   │
       └─────┬───┘    └──────────┘
             │ selects at probe time
  ┌──────────┼────────────────────────────────────────────────┐
  │  Per-Device Top Layers                                     │
  │                                                           │
  │  data_dev/driver/src/       rce_stream/driver/src/        │
  │  data_dev_top.c             rce_top.c                     │
  │  PCI driver                 Platform driver               │
  │  struct pci_driver          struct platform_driver        │
  │  vendor 0x1a4a / 0x2030     of: "axi_stream_dma"         │
  │  Up to 32 devices           Up to 4 devices               │
  │                                                           │
  │  rce_hp_buffers/driver/src/ rce_memmap/driver/src/        │
  │  rce_hp.c + rce_top.c       rce_map.c                     │
  │  Platform driver (1 dev)    Platform driver               │
  │  HP buffer management       Raw memory-map /dev node      │
  └──────────────────────────────────────────────────────────-┘
             │
  ┌──────────▼──────────────────────────────────┐
  │  FPGA / PCIe Hardware                        │
  │  AXI Stream DMA engine (Gen1 or Gen2)        │
  │  AxiVersion registers                        │
  │  User-space MMIO window                      │
  └─────────────────────────────────────────────┘
```

## Major Components

### Common DMA Layer (`common/driver/`)

The shared kernel library used by every driver variant.

**`dma_common.c` / `dma_common.h`**
- Owns the Linux `file_operations` struct (`DmaFunctions`), wiring `read`, `write`, `ioctl`, `poll`, `mmap`, `fasync` to generic DMA implementations.
- Implements `Dma_Init` / `Dma_Clean` — lifecycle entry points called from per-device Probe/Remove.
- Maintains a global `gDmaDevices[]` array (max per-driver) and `gDmaDevCount`.
- Exposes a `/proc/<devname>` entry via `seq_file` for runtime diagnostics.
- Central struct: `DmaDevice` — holds MMIO base addresses, config knobs (`cfgTxCount`, `cfgRxCount`, `cfgSize`, `cfgMode`), buffer lists, transmit queue, spinlocks, and the `hwFunc` vtable pointer.
- Central per-open struct: `DmaDesc` — per-file-descriptor destination mask and receive queue. Allocated at `Dma_Open`, freed at `Dma_Release`.

**`dma_buffer.c` / `dma_buffer.h`**
- Provides DMA-mapped buffer pool management: `dmaAllocBuffers`, `dmaFreeBuffers`.
- Implements `DmaBufferList` (indexed + sorted arrays for O(log n) lookup by DMA handle) and `DmaQueue` (lock-protected circular buffer queue with wait-queue support).
- Supports three buffer modes: `BUFF_COHERENT` (dma_alloc_coherent), `BUFF_STREAM` (dma_map_single), `BUFF_ARM_ACP` (ARM accelerator coherency port).
- Up to 100,000 buffers per list (`BUFFERS_PER_LIST`).

**`axis_gen2.c` / `axis_gen2.h`**
- Implements the `hardware_functions` vtable for AXIS Gen2 DMA engine.
- Uses a descriptor ring (`AxisG2Reg.dmaAddr[4096]`) with hardware write/read index registers.
- Supports 64-bit and 128-bit descriptors (`desc128En`).
- Runs an optional workqueue (`wq`) for IRQ-free polling mode and deferred IRQ processing.
- Exports `AxisG2_functions` struct.

**`axis_gen1.c` / `axis_gen1.h`**
- Implements the `hardware_functions` vtable for AXIS Gen1 DMA engine (legacy RCE hardware).
- Register-FIFO-based TX/RX (push address into `txPostA/B/C`, pop from `rxPend`).
- Exports `AxisG1_functions` struct.

**`axi_version.c` / `axi_version.h`**
- Read-only access to the `AxiVersion_Reg` register block (firmware version, git hash, build string, device DNA, user reset).
- Shared by data_dev and rce_stream.

**`gpu_async.c` / `gpu_async.h`**
- Optional NVIDIA GPUDirect peer-to-peer DMA path, compiled in with `-DDATA_GPU=1`.
- Uses NVIDIA `nvidia_p2p_*` kernel API to pin GPU memory pages and build DMA mappings.
- Up to 1024 GPU buffers per direction (`MAX_GPU_BUFFERS`).

### Per-Device Top Layers

**`data_dev/driver/src/data_dev_top.c`**
- PCI driver (`struct pci_driver DataDevDriver`).
- Matches PCI vendor `0x1a4a` / device `0x2030` (SLAC FPGA PCIe card).
- Sets `dev->hwFunc = &AxisG2_functions` (Gen2 only).
- Configures register sub-windows from BAR0:
  - `0x00000000` — Gen2 DMA engine (`dev->reg`)
  - `0x00010000` — PCIe PHY
  - `0x00020000` — AxiVersion
  - `0x00030000` — PROM
  - `0x00800000` — User MMIO (`dev->rwBase`, exposed via `mmap`)
- Supports up to 32 simultaneous devices.
- Implements `DataDev_Command` dispatching GPU commands, AxiVersion reads, and AXIS Gen2 commands.
- Module name: `datadev`. Module parameters: `cfgTxCount`, `cfgRxCount`, `cfgSize`, `cfgMode`, `cfgCont`, `cfgIrqHold`, `cfgBgThold[0-7]`, `cfgDevName`, `cfgTimeout`.

**`rce_stream/driver/src/rce_top.c`**
- Platform driver (`struct platform_driver`), Device Tree matched as `"axi_stream_dma"`.
- Selects `AxisG2_functions` or `AxisG1_functions` at probe time by reading the hardware version register.
- Supports up to 4 devices named `axi_stream_dma_0..3`, each with independent buffer counts/sizes/modes.
- Module name: `axi_stream_dma`.

**`rce_hp_buffers/driver/src/`**
- Platform driver for RCE "High-Performance" buffer hardware (single device, `MAX_DMA_DEVICES=1`).
- Implements its own `hardware_functions` in `rce_hp.c` (`RceHp_functions`).
- Hardware register block: enable, bufferClear, bufferSize, bufferAlloc at offset `0x400`.

**`rce_memmap/driver/src/rce_map.c`**
- Standalone platform module (`rce_memmap`), does NOT use `DmaDevice` or `hardware_functions`.
- Exposes `/dev/rce_memmap` for raw MMIO read/write access to a physical address window (`cfgMinAddr`–`cfgMaxAddr`, default `0x80000000`–`0xBFFFFFFF`).
- Used for register-level access on RCE boards without DMA.

### User-Space API Layer (`include/`)

- `DmaDriver.h` — dual-use header (kernel + userspace via `#ifdef DMA_IN_KERNEL`). Defines `DmaWriteData`, `DmaReadData`, `DmaRegisterData` structs; all `DMA_*` ioctl command codes; inline `dmaWrite`, `dmaRead`, `dmaWriteIndex`, `dmaReadIndex` functions.
- `AxisDriver.h` — AXIS-specific ioctl codes (`AXIS_Read_Ack`, `AXIS_Write_ReqMissed`) and inline flag pack/unpack (`axisSetFlags`, `axisGetFuser`, `axisGetLuser`, `axisGetCont`).
- `DataDriver.h` — convenience header that includes `AxisDriver.h`, `DmaDriver.h`, `AxiVersion.h`.
- `AxiVersion.h` — userspace struct for firmware version readback.
- `GpuAsync.h` / `GpuAsyncRegs.h` / `GpuAsyncUser.h` — GPUDirect ioctl commands and register definitions.

## Data Flow

### RX Path (Hardware to User)

1. Hardware DMA engine writes a received frame into a pre-posted `DmaBuffer` (previously given to hardware via `retRxBuffer` / `AxisG2_WriteFree`).
2. Hardware asserts interrupt (or workqueue polls).
3. IRQ handler (`AxisG2_Irq` / `AxisG1_Irq`) calls `AxisG2_Process`, which reads the hardware return FIFO.
4. For each completed RX entry, `dmaRxBufferIrq` is called: buffer is marked `inHw=0`, destination field is set, and the buffer is pushed onto the matching `DmaDesc.q` (per open file-descriptor receive queue).
5. Any `poll`/`select` waiters on that `DmaDesc` are woken.
6. User calls `read()` -> `Dma_Read` -> `dmaQueuePopList` drains the per-descriptor queue.
7. If user passed a pointer in `DmaReadData.data`, kernel `copy_to_user` transfers data and `retRxBuffer` recycles the buffer. If pointer is NULL (zero-copy), buffer stays user-owned (`buff->userHas = desc`) until returned via `DMA_Ret_Index` ioctl.

### TX Path (User to Hardware)

1. User calls `write()` with a `DmaWriteData` struct.
2. `Dma_Write` validates size and destination channel.
3. If `data` pointer is set: pop a free TX buffer from `dev->tq`, `copy_from_user` data into it.
4. If `data` is NULL: locate buffer by `DmaWriteData.index` (zero-copy mmap'd path).
5. Set `buff->dest`, `buff->flags`, `buff->size`.
6. Call `dev->hwFunc->sendBuffer` -> `AxisG2_SendBuffer` -> pushes buffer DMA address into hardware write FIFO.

### Ioctl Dispatch

```
Dma_Ioctl(cmd & 0xFFFF)
  |-- DMA_Get_Buff_Count / DMA_Get_Buff_Size / DMA_Get_Version   return counts/config
  |-- DMA_Set_Mask / DMA_Set_MaskBytes                           update DmaDesc.destMask,
  |                                                              update DmaDevice.desc[] routing table
  |-- DMA_Ret_Index                                              return user-held RX buffer to HW
  |-- DMA_Get_Index                                              get TX buffer index (mmap path)
  |-- DMA_Read_Ready                                             poll DmaDesc queue non-blocking
  |-- DMA_Write_Register / DMA_Read_Register                     direct MMIO access via rwBase
  `-- (cmd > 0x2000)                                             dev->hwFunc->command()
        |-- AxisG2_Command (AXIS_Read_Ack, Gen2-specific)
        |-- DataDev_Command (AxiVersion, GPU commands)
        `-- RceHp_Command / AxisG1_Command (RCE variants)
```

### DMA Buffer Lifecycle

```
  dmaAllocBuffers()
       |
       v
  [free TX pool: dev->tq]   [RX pool: posted to HW via retRxBuffer]
       |                              |
  dmaQueuePop (on write)         DMA hardware fills buffer
       |                              |
  sendBuffer -> HW FIFO          IRQ: dmaRxBufferIrq -> DmaDesc.q
       |                              |
  HW DMA out                    dmaQueuePopList (on read)
                                      |
                                 user reads / or holds index
                                      |
                                 retRxBuffer -> back to HW
```

## Driver Hierarchy

```
include/                       <- Dual-use API headers (kernel + userspace)
  DmaDriver.h                  <- Core ioctl API, DmaWriteData/DmaReadData
  AxisDriver.h                 <- AXIS flags + AXIS_Read_Ack ioctl
  DataDriver.h                 <- Umbrella include for data_dev users
  AxiVersion.h                 <- Firmware version struct
  GpuAsync.h                   <- GPUDirect ioctl interface

common/driver/                 <- Shared kernel library (no module_init here)
  dma_common.c/h               <- file_operations, DmaDevice, DmaDesc
  dma_buffer.c/h               <- DmaBuffer, DmaBufferList, DmaQueue
  axis_gen2.c/h                <- hardware_functions for Gen2 AXIS DMA
  axis_gen1.c/h                <- hardware_functions for Gen1 AXIS DMA
  axi_version.c/h              <- AxiVersion register access
  gpu_async.c/h                <- NVIDIA GPUDirect (optional, -DDATA_GPU=1)

data_dev/driver/src/           <- "datadev" kernel module (PCIe)
  data_dev_top.c/h             <- pci_driver, Probe/Remove, DataDev_Command

rce_stream/driver/src/         <- "axi_stream_dma" kernel module (platform)
  rce_top.c/h                  <- platform_driver, auto-selects Gen1 or Gen2

rce_hp_buffers/driver/src/     <- HP buffer platform driver (1 device)
  rce_hp.c/h                   <- hardware_functions for HP buffer hardware
  rce_top.c/h                  <- platform_driver entry point

rce_memmap/driver/src/         <- "rce_memmap" standalone module
  rce_map.c/h                  <- independent char device, no DmaDevice
```

### hardware_functions Vtable

Every driver using the common DMA layer plugs a `struct hardware_functions` into `DmaDevice.hwFunc`:

| Field         | Purpose                                       |
|---------------|-----------------------------------------------|
| `irq`         | Top-half IRQ handler                          |
| `init`        | Hardware-specific init (called from Dma_Init) |
| `enable`      | Enable DMA after init                         |
| `irqEnable`   | Mask/unmask hardware interrupts               |
| `clear`       | Quiesce hardware on removal                   |
| `retRxBuffer` | Return one or more RX buffers to hardware     |
| `sendBuffer`  | Submit one or more TX buffers to hardware     |
| `command`     | Dispatch hardware-specific ioctl commands     |
| `seqShow`     | Print hardware state to /proc entry           |

Concrete implementations: `AxisG2_functions`, `AxisG1_functions`, `RceHp_functions`, `DataDev_functions` (wraps AxisG2 + adds AxiVersion/GPU dispatch).

## Key Design Patterns

### Per-Open Descriptor Pattern
Each `open()` allocates a `DmaDesc` stored in `filp->private_data`. The descriptor holds a bitmask (`destMask`, 512 bytes = 4096 bits) of DMA destination channels that this file handle will receive. `DmaDevice.desc[dest]` is a sparse pointer array routing arriving frames to the correct open file descriptor.

### Zero-Copy mmap Path
TX and RX buffers are DMA-allocated at module load. User applications can `mmap` them directly. The write path accepts a buffer index instead of a pointer (`DmaWriteData.index` with `DmaWriteData.data = 0`). The read path can return a buffer index and leave the buffer mapped in user space until `DMA_Ret_Index` is called.

### Hardware Abstraction via Vtable
The `hardware_functions` struct fully isolates common DMA logic from hardware specifics. Adding a new hardware variant requires: a new top file, a new `hardware_functions` implementation, and a Makefile entry.

### Build-Time Source Inclusion
Rather than building `common/driver/` as a shared kernel object, each driver module links the common `.c` source files directly into its own Kbuild object list. This avoids kernel export symbol complexity and allows each module to be self-contained.

### Destination Channel Routing
The DMA destination field (up to 4096 virtual channels, `DMA_MAX_DEST = 8 * DMA_MASK_SIZE = 4096`) maps to per-open receive queues. This allows a single physical device to multiplex across many user processes.

### IRQ Holdoff and Background Thresholds
Gen2 hardware supports IRQ holdoff (`cfgIrqHold`, default 10000 cycles) and eight background-mode count thresholds (`cfgBgThold[0-7]`) to coalesce interrupts for high-throughput workloads. The `AxisG2Data.wq` workqueue defers processing out of interrupt context.

## Error Handling

**Initialization:** Sequential `goto` cleanup labels in `Dma_Init` and `DataDev_Probe` unwind resources in reverse-allocation order (e.g., `err_post_en` -> `cleanup_alloc_chrdev_region`).

**Runtime:** `dev_warn` / `dev_err` macros log issues. Read/write syscalls return `-1` or negative errno. `DmaReadData.error` reports per-buffer error bits: `DMA_ERR_FIFO` (0x01), `DMA_ERR_LEN` (0x02), `DMA_ERR_MAX` (0x04), `DMA_ERR_BUS` (0x08).

**IRQ path:** Spinlocks (`writeHwLock`, `maskLock`) protect hardware register writes and destination mask updates. `_Irq` suffix functions are safe to call from interrupt context.

## Cross-Cutting Concerns

**Logging:** `dev_info` / `dev_warn` / `dev_err` macros throughout. Debug verbosity controlled by `dev->debug` (set via `DMA_Set_Debug` ioctl or `cfgDebug` module parameter).

**Kernel version compatibility:** Conditional compilation handles API changes at v2.6.25, v4.16, v5.6.0, v5.15.0, v6.4.0, and RHEL 9.4 backports. The `__poll_t` typedef is injected for kernels older than 4.16; `class_create` signature changed at 6.4.

**DMA addressing:** AXI address width is probed from hardware at `dev->reg + 0x34`; the DMA mask is set accordingly (up to 64-bit).

**DKMS support:** `data_dev/driver/dkms.conf` and `data_dev/driver/dkms-gpu.conf` allow automatic rebuild on kernel updates.

---

*Architecture analysis: 2026-03-26*
