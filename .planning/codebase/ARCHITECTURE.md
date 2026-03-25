# Architecture

**Analysis Date:** 2026-03-25

## Pattern Overview

**Overall:** Linux Kernel Module with User-Space API

**Key Characteristics:**
- Character device driver interface with standard file operations (open, read, write, ioctl, poll, mmap, release)
- Platform driver (rce_stream, rce_memmap) and PCI driver (data_dev) architectures
- Hardware abstraction layer via `hardware_functions` structure for board-specific operations
- Shared common library for DMA buffer management, queues, and common functions
- Cross-platform support for x86_64 (data_dev with NVIDIA GPU) and ARM (RCE)

## Layers

**Application Layer:**
- Purpose: User-space applications for testing, benchmarking, and data transfer
- Location: `common/app/`, `data_dev/app/`, `rce_stream/app/`
- Contains: C++ test applications (dmaLoopTest, dmaRead, dmaWrite, test)
- Depends on: User-space driver library (`include/DmaDriver.h`, `include/AxisDriver.h`)
- Used by: Test infrastructure and integration scripts

**User-Space Driver Library:**
- Purpose: User-space API for kernel driver communication
- Location: `include/` (DmaDriver.h, AxisDriver.h, AxiVersion.h, GpuAsync.h)
- Contains: ioctl command definitions, data structures (DmaWriteData, DmaReadData), inline helper functions
- Pattern: Header-only library with conditional compilation for kernel vs user space

**Kernel Driver Core (Common):**
- Purpose: Shared DMA infrastructure and buffer management
- Location: `common/driver/`
- Contains:
  - `dma_common.c/h`: Core DMA device initialization, file operations, buffer management
  - `dma_buffer.c/h`: Buffer allocation, tracking, and queue management
  - `axis_gen1.c/h`, `axis_gen2.c/h`: AXI stream protocol implementations
  - `axi_version.c/h`: AXI version register access
- Depends on: Linux kernel APIs (pci, platform_driver, dma, irq)

**Hardware Abstraction Layer:**
- Purpose: Board-specific hardware operations
- Location: Per-board driver directories
- Contains: Hardware-specific implementations of `hardware_functions` structure
- Pattern: Driver core calls hwFunc->init, hwFunc->sendBuffer, hwFunc->irq, etc.

**Hardware Layer:**
- Purpose: Physical PCIe devices and FPGA AXI interfaces
- Data_dev: TID-AIR PCIe cards with optional NVIDIA GPU Direct RDMA
- RCE Stream: RCE AXI stream DMA controller
- RCE Memmap: RCE memory mapping interface

## Data Flow

**DMA Write Flow (Software -> Firmware):**

1. User calls `dmaWrite()` or `dmaWriteIndex()` from application
2. `Dma_Write()` kernel function receives write request
3. Buffer retrieved from transmit queue (`dmaQueuePop(&(dev->tq))`)
4. Data copied from user space to DMA buffer
5. Hardware-specific `sendBuffer()` called (e.g., `AxisG1_SendBuffer`)
6. Buffer handle written to hardware TX FIFOs
7. Interrupt handler (`AxisG1_Irq`) receives completion status
8. Buffer returned to queue for reuse

**DMA Read Flow (Firmware -> Software):**

1. Hardware receives data via AXI stream interface
2. Interrupt handler (`AxisG1_Irq`) reads from RX FIFOs
3. Buffer located in buffer list (`dmaFindBufferList`)
4. Buffer populated with size, flags, destination, error info
5. Buffer pushed to descriptor's RX queue (`dmaRxBuffer`)
6. User calls `dmaRead()` or `dmaReadIndex()`
7. `Dma_Read()` pops from descriptor queue and copies to user space
8. Buffer returned to hardware RX pool

**State Management:**
- Buffer ownership tracked per-descriptor via `userHas` pointer
- Hardware queue state managed via `inHw`, `inQ` flags on each buffer
- Destination masking via `destMask` to reserve lanes for specific consumers

## Key Abstractions

**DmaDevice:**
- Purpose: Represents a single DMA-capable device instance
- Location: `common/driver/dma_common.h`
- Contains: PCI device, register mappings, buffer lists, configuration, hardware functions
- Pattern: Global array `gDmaDevices[]` with count tracking

**DmaDesc (Descriptor):**
- Purpose: Per-file-descriptor state for DMA operations
- Location: `common/driver/dma_common.h`
- Contains: Destination mask, RX queue, async notification queue
- Pattern: Allocated in `open()`, freed in `release()`

**hardware_functions:**
- Purpose: Board-specific operation callbacks
- Location: `common/driver/dma_common.h`
- Fields: irq, init, enable, clear, irqEnable, retRxBuffer, sendBuffer, command, seqShow
- Pattern: Each driver defines its own implementation structure

**DmaBuffer:**
- Purpose: Managed memory region for DMA transfers
- Location: `common/driver/dma_buffer.h`
- Contains: Virtual/physical addresses, size, index, ownership tracking
- Pattern: Allocated in bulk during device initialization

## Entry Points

**PCI Probe (data_dev):**
- Location: `data_dev/driver/src/data_dev_top.c:DataDev_Probe`
- Triggers: PCI device enumeration finds matching device
- Responsibilities: Allocate DmaDevice, map registers, initialize buffers, register char device

**Platform Probe (rce_stream):**
- Location: `rce_stream/driver/src/rce_top.c:Rce_Probe`
- Triggers: Device tree match for "axi_stream_dma" compatible device
- Responsibilities: Same as PCI probe, uses of_platform API

**File Operations:**
- Location: `common/driver/dma_common.c:Dma_Open`, `Dma_Close`, `Dma_Read`, `Dma_Write`, `Dma_Ioctl`
- Triggers: User-space system calls on `/dev/datadev_*`
- Responsibilities: Request handling, buffer management, ioctl command dispatch

## Error Handling

**Strategy:** Return negative error codes to userspace; log errors via `dev_err()`, warnings via `dev_warn()`

**Patterns:**
- Memory allocation failures return `-ENOMEM`
- Invalid parameters return `-EINVAL` (or `-1` in legacy code)
- Hardware errors logged with descriptive messages
- Buffer errors tracked via error bits in `DmaReadData.error`

## Cross-Cutting Concerns

**Logging:** Uses kernel `printk` macros (`dev_info`, `dev_warn`, `dev_err`) with device pointers
**Validation:** Buffer sizes checked against `cfgSize`; addresses validated against mapped regions
**Synchronization:** Spinlocks for hardware write, command, and mask operations; prevents race conditions

---

*Architecture analysis: 2026-03-25*