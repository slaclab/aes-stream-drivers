# Codebase Structure

**Analysis Date:** 2026-03-26

## Directory Layout

```
aes-stream-drivers/
|-- include/                     # Dual-use API headers (kernel + userspace)
|-- common/
|   |-- driver/                  # Shared kernel driver library (no module_init)
|   |-- app/                     # Shared userspace test utilities
|   `-- app_lib/                 # Shared userspace helper library sources
|-- data_dev/
|   |-- driver/                  # "datadev" PCIe kernel module
|   |   |-- src/                 # Driver C sources
|   |   `-- Makefile
|   `-- app/
|       |-- src/                 # Userspace test app sources (C++)
|       |-- bin/                 # Compiled test binaries
|       `-- Makefile
|-- rce_stream/
|   |-- driver/                  # "axi_stream_dma" platform kernel module
|   |   |-- src/
|   |   `-- Makefile
|   `-- app/
|       |-- src/
|       `-- Makefile
|-- rce_hp_buffers/
|   `-- driver/                  # HP buffer platform kernel module
|       |-- src/
|       `-- Makefile
|-- rce_memmap/
|   `-- driver/                  # "rce_memmap" MMIO access module
|       |-- src/
|       `-- Makefile
|-- scripts/                     # Helper scripts (load, install)
|-- Yocto/
|   `-- recipes-kernel/          # Yocto BitBake recipes for RCE builds
|       |-- axistreamdma/
|       `-- aximemorymap/
|-- docs/                        # Additional documentation
|-- Makefile                     # Top-level: targets app, driver, rce
|-- .clang-tidy                  # Clang-tidy lint configuration
|-- .clangd                      # clangd LSP configuration
|-- CPPLINT.cfg                  # cpplint configuration
|-- .editorconfig                # Editor formatting settings
`-- .github/
    `-- workflows/               # CI workflow definitions
```

## Directory Purposes

### `include/`
Purpose: Dual-use public API headers shared between kernel driver code and userspace applications.

Key files:
- `include/DmaDriver.h` — Core API. Defines `DmaWriteData`, `DmaReadData`, `DmaRegisterData` structs; all `DMA_*` ioctl command codes (`DMA_Get_Buff_Count` through `DMA_Get_GITV`); inline `dmaWrite`, `dmaRead`, `dmaWriteIndex`, `dmaReadIndex`, `dmaGetVersion` functions (userspace only). Also defines `DMA_VERSION`, error bit constants, and `DMA_MASK_SIZE`.
- `include/AxisDriver.h` — AXIS stream extensions: `AXIS_Read_Ack` and `AXIS_Write_ReqMissed` ioctl codes; inline `axisSetFlags`, `axisGetFuser`, `axisGetLuser`, `axisGetCont`.
- `include/DataDriver.h` — Convenience umbrella include (`AxisDriver.h` + `DmaDriver.h` + `AxiVersion.h`). Use this in data_dev applications.
- `include/AxiVersion.h` — Userspace struct for firmware version readback (firmwareVersion, gitHash, buildString, deviceId, etc.).
- `include/GpuAsync.h` — GPUDirect ioctl command codes (`GPU_Add_Nvidia_Memory`, `GPU_Rem_Nvidia_Memory`, `GPU_Set_Write_Enable`, `GPU_Get_Gpu_Async_Ver`, `GPU_Get_Max_Buffers`).
- `include/GpuAsyncRegs.h` — GPU async register layout definitions.
- `include/GpuAsyncUser.h` — GPU async userspace structures.

### `common/driver/`
Purpose: The kernel library that implements all shared DMA logic. Sources are compiled directly into each per-device module rather than as a standalone `.ko`.

Key files:
- `common/driver/dma_common.c` / `dma_common.h` — Central DMA framework. Defines `DmaDevice`, `DmaDesc`, `hardware_functions`. Implements all `file_operations` (`Dma_Read`, `Dma_Write`, `Dma_Ioctl`, `Dma_Poll`, `Dma_Mmap`, `Dma_Open`, `Dma_Release`, `Dma_Fasync`), `Dma_Init`, `Dma_Clean`, `/proc` seq_file interface.
- `common/driver/dma_buffer.c` / `dma_buffer.h` — Buffer allocation, `DmaBufferList` management, `DmaQueue` circular queue with spinlock and wait-queue.
- `common/driver/axis_gen2.c` / `axis_gen2.h` — Gen2 AXIS DMA hardware implementation. Defines `AxisG2Reg` register map, `AxisG2Data` runtime state, `AxisG2_functions`. Exports `AxisG2_Irq`, `AxisG2_Process`, `AxisG2_Init`, `AxisG2_Enable`, `AxisG2_Clear`, `AxisG2_SendBuffer`, `AxisG2_RetRxBuffer`.
- `common/driver/axis_gen1.c` / `axis_gen1.h` — Gen1 (legacy) AXIS DMA hardware implementation. Defines `AxisG1Reg`, `AxisG1_functions`.
- `common/driver/axi_version.c` / `axi_version.h` — Reads `AxiVersion_Reg` block (firmware version, git hash, build string, device DNA). Used by data_dev and rce_stream.
- `common/driver/gpu_async.c` / `gpu_async.h` — NVIDIA GPUDirect P2P path. Requires `nvidia_p2p_*` symbols. Compiled only when `NVIDIA_DRIVERS` is set and `-DDATA_GPU=1`.

### `common/app/` and `common/app_lib/`
Purpose: Shared userspace test application source files and a helper library.

Key files:
- `common/app/dmaRead.cpp`, `dmaWrite.cpp`, `dmaLoopTest.cpp`, `dmaSetDebug.cpp` — Generic DMA test programs.
- `common/app_lib/AppUtils.h` — Shared utility functions for test apps.
- `common/app_lib/PrbsData.cpp` / `PrbsData.h` — PRBS (pseudo-random bit sequence) data generator/checker for link testing.

### `data_dev/driver/src/`
Purpose: The `datadev` PCIe kernel module. PCI IDs: vendor `0x1a4a`, device `0x2030`.

Key files:
- `data_dev/driver/src/data_dev_top.c` — `module_init(DataDev_Init)`, `module_exit(DataDev_Exit)`. `DataDev_Probe` sets up BAR0 sub-regions, calls `Dma_Init`. `DataDev_Command` dispatches device-specific ioctls (AxiVersion, GPU). `DataDev_functions` is the `hardware_functions` instance (delegates most ops to AxisG2).
- `data_dev/driver/src/data_dev_top.h` — `MAX_DMA_DEVICES=32`, BAR0 offset constants, PCI IDs.
- `data_dev/driver/src/dma_common.c` — Private copy of common source (symlinked or copied).
- `data_dev/driver/src/dma_buffer.c` — Private copy of common source.
- `data_dev/driver/src/axis_gen2.c` — Private copy of common source.
- `data_dev/driver/src/axi_version.c` — Private copy of common source.
- `data_dev/driver/src/gpu_async.c` — GPU support (included when `NVIDIA_DRIVERS` is defined).
- `data_dev/driver/Makefile` — Builds `datadev.ko`. Sets `-DDMA_IN_KERNEL=1 -DPCIE_DMA=1`. Optionally adds `-DDATA_GPU=1` for GPU builds. Supports DKMS via `dkms.conf`.
- `data_dev/driver/dkms.conf` — DKMS config for standard builds.
- `data_dev/driver/dkms-gpu.conf` — DKMS config for GPU-enabled builds.

### `data_dev/app/src/`
Purpose: Userspace test applications for data_dev.

Key files:
- `data_dev/app/src/dmaRead.cpp` — Read test using `DmaDriver.h` API.
- `data_dev/app/src/dmaWrite.cpp` — Write test.
- `data_dev/app/src/dmaLoopTest.cpp` — Loopback test.
- `data_dev/app/src/dmaSetDebug.cpp` — Sets debug level via `DMA_Set_Debug` ioctl.
- `data_dev/app/src/dmaRate.cpp` — Throughput benchmark.
- `data_dev/app/src/rdmaTest.cu` — CUDA/GPUDirect test (requires NVIDIA toolkit).
- `data_dev/app/src/test.cpp` — Miscellaneous test.

### `rce_stream/driver/src/`
Purpose: The `axi_stream_dma` platform kernel module for RCE (Zynq/ARM) boards.

Key files:
- `rce_stream/driver/src/rce_top.c` — `module_platform_driver(Rce_DmaPdrv)`. Device Tree match: `"axi_stream_dma"`. Supports devices `axi_stream_dma_0..3`. Probes Gen2 vs Gen1 at runtime by reading the version register. Per-instance buffer config (index 0/1/2 get different sizes and modes).
- `rce_stream/driver/src/rce_top.h` — `MAX_DMA_DEVICES=4`.
- `rce_stream/driver/src/axis_gen1.c` / `axis_gen2.c` — Private copies of common Gen1/Gen2 implementations.
- `rce_stream/driver/src/dma_common.c` / `dma_buffer.c` — Private copies of common layer.

### `rce_hp_buffers/driver/src/`
Purpose: Platform driver for RCE High-Performance buffer hardware.

Key files:
- `rce_hp_buffers/driver/src/rce_hp.c` — Hardware-specific `hardware_functions` implementation (`RceHp_functions`). Interacts with `RceHpReg` at offset `0x400` (enable, bufferClear, bufferSize, bufferAlloc).
- `rce_hp_buffers/driver/src/rce_hp.h` — `RceHpReg` register struct, function prototypes.
- `rce_hp_buffers/driver/src/rce_top.c` / `rce_top.h` — Platform driver entry. `MAX_DMA_DEVICES=1`.
- `rce_hp_buffers/driver/src/dma_common.c` / `dma_buffer.c` — Private copies of common layer.

### `rce_memmap/driver/src/`
Purpose: Standalone MMIO character device module. Does NOT use the DmaDevice framework.

Key files:
- `rce_memmap/driver/src/rce_map.c` — Module init, `/dev/rce_memmap` char device, `Map_Read`/`Map_Write`/`Map_Ioctl`. Maps physical addresses in range `cfgMinAddr`–`cfgMaxAddr` into kernel vmalloc'd 64 KB windows (`MAP_SIZE = 0x10000`) on demand.
- `rce_memmap/driver/src/rce_map.h` — `MemMap`, `MapDevice` structs; file operation prototypes.

### `scripts/`
Purpose: Shell scripts for loading and installing drivers.

### `Yocto/`
Purpose: BitBake recipes for building RCE drivers as part of a Yocto Linux image.

Key files:
- `Yocto/recipes-kernel/axistreamdma/` — Recipe for `axi_stream_dma` module.
- `Yocto/recipes-kernel/aximemorymap/` — Recipe for `rce_memmap` module.
- `Yocto/recipes-tests/axidmasamples/` — Recipe for userspace test apps.

## Key Files and Their Purposes

| File | Purpose |
|------|---------|
| `include/DmaDriver.h` | Primary userspace API — include in all applications |
| `include/DataDriver.h` | Umbrella include for data_dev applications |
| `common/driver/dma_common.h` | Central kernel struct definitions (`DmaDevice`, `DmaDesc`, `hardware_functions`) |
| `common/driver/dma_buffer.h` | Buffer pool and queue struct definitions |
| `common/driver/axis_gen2.h` | Gen2 register map and runtime data structs |
| `data_dev/driver/src/data_dev_top.c` | PCIe module entry point and command dispatch |
| `rce_stream/driver/src/rce_top.c` | RCE platform module entry point |
| `data_dev/driver/Makefile` | Primary build file; controls GPU/non-GPU builds |
| `Makefile` | Top-level orchestration (`app`, `driver`, `rce` targets) |

## Module Boundaries

Each kernel module is a self-contained `.ko` file. Common code is compiled in directly.

| Module | File | Bus Type | Max Devices | Hardware |
|--------|------|----------|-------------|----------|
| `datadev` | `data_dev/driver/` | PCIe | 32 | SLAC FPGA (Gen2 only) |
| `axi_stream_dma` | `rce_stream/driver/` | Platform/DT | 4 | RCE (Gen1 or Gen2) |
| `rce_hp_buffers` | `rce_hp_buffers/driver/` | Platform/DT | 1 | RCE HP buffer block |
| `rce_memmap` | `rce_memmap/driver/` | Platform/DT | 1 | Raw MMIO (no DMA) |

## Naming Conventions

**Files:**
- Kernel source: `snake_case.c` / `snake_case.h` (e.g., `dma_common.c`, `axis_gen2.h`)
- Userspace headers: `PascalCase.h` (e.g., `DmaDriver.h`, `AxiVersion.h`)
- Userspace apps: `camelCase.cpp` (e.g., `dmaRead.cpp`, `dmaLoopTest.cpp`)

**Structs:**
- Kernel internal: `DmaDevice`, `DmaDesc`, `DmaBuffer`, `DmaQueue`, `AxisG2Data` (PascalCase)
- Register maps: `AxisG2Reg`, `AxisG1Reg`, `RceHpReg`, `AxiVersion_Reg` (PascalCase with optional `_Reg` suffix)

**Functions:**
- Common DMA layer: `Dma_` prefix (e.g., `Dma_Init`, `Dma_Read`, `Dma_Ioctl`)
- Gen2 hardware functions: `AxisG2_` prefix (e.g., `AxisG2_Irq`, `AxisG2_SendBuffer`)
- Gen1 hardware functions: `AxisG1_` prefix
- HP buffer functions: `RceHp_` prefix
- Buffer/queue operations: `dma` camelCase (e.g., `dmaAllocBuffers`, `dmaQueuePush`)
- DataDev top-level: `DataDev_` prefix

## Where to Add New Code

**New hardware variant (new FPGA design or board):**
- Create `<name>/driver/src/<name>_top.c` and `<name>_top.h`
- Implement all nine fields of `struct hardware_functions`
- Call `Dma_Init` from probe, `Dma_Clean` from remove
- Write a Kbuild-style `Makefile` listing the common sources plus your top file
- Add userspace apps in `<name>/app/src/` using `include/DmaDriver.h`

**New ioctl command:**
- Define the command code in `include/DmaDriver.h` (userspace-visible) or the relevant `include/*.h`
- Handle it in `dev->hwFunc->command()` in the appropriate top-level `*_Command` function
- For generic DMA commands (buffer counts, masks): add a case to `Dma_Ioctl` in `common/driver/dma_common.c`

**New userspace application:**
- Place in `data_dev/app/src/` (for PCIe) or `rce_stream/app/src/` (for RCE)
- Include `<DataDriver.h>` or `<DmaDriver.h>` from `include/`
- Add the binary to the `Makefile` in that `app/` directory

**New test utility (shared across drivers):**
- Place source in `common/app/` or `common/app_lib/`
- Reference from per-driver app Makefiles

## Special Directories

**`data_dev/app/.obj/`**
- Purpose: Intermediate object files for the userspace application build
- Generated: Yes
- Committed: No (in `.gitignore`)

**`data_dev/driver/install/` (created at build time)**
- Purpose: Staging area for built `.ko` files per kernel version
- Generated: Yes
- Committed: No

**`.planning/codebase/`**
- Purpose: Codebase analysis documents for AI-assisted development workflow
- Generated: Yes (by GSD tooling)
- Committed: Yes

---

*Structure analysis: 2026-03-26*
