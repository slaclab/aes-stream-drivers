# Codebase Structure

**Analysis Date:** 2026-03-25

## Directory Layout

```
aes-stream-drivers/
├── common/                    # Shared drivers, libraries, and applications
│   ├── app/                   # Common test applications
│   ├── app_lib/               # Application library code (e.g., PRBS data)
│   └── driver/                # Common driver infrastructure
├── data_dev/                  # Data Device (PCIe) driver and apps
│   ├── app/                   # Data device applications (symlinks to common)
│   ├── driver/                # Data device kernel driver
│   └── scripts/               # Data device scripts
├── rce_stream/                # RCE AXI Stream driver and apps
│   ├── app/                   # RCE stream applications
│   └── driver/                # RCE stream kernel driver
├── rce_memmap/                # RCE Memory Map driver
│   └── driver/                # RCE memmap kernel driver
├── rce_hp_buffers/            # RCE High Performance Buffers driver
│   └── driver/                # RCE HP buffers kernel driver
├── include/                   # Top-level application headers
├── docs/                      # Documentation files
├── scripts/                   # Utility scripts (clang-tidy, uploadTag, etc.)
├── Yocto/                     # Yocto BitBake recipes
├── Makefile                   # Top-level build file
└── README.md                  # Project overview
```

## Directory Purposes

**common/**
- Purpose: Houses shared infrastructure used across all driver variants
- Contains:
  - `app/`: dmaLoopTest, dmaRead, dmaSetDebug, dmaWrite (main test applications)
  - `app_lib/`: PrbsData (Pseudo-Random Binary Sequence generation/checking)
  - `driver/`: dma_common, dma_buffer, axis_gen1, axis_gen2, axi_version
- Key files: `common/driver/dma_common.c`, `common/driver/dma_buffer.c`

**common/driver/**
- Purpose: Core DMA infrastructure shared by all drivers
- Contains:
  - `dma_common.c/h`: Character device setup, file ops, buffer queues
  - `dma_buffer.c/h`: Buffer pool management, allocation, queue operations
  - `axis_gen1.c/h`: Gen1 AXI stream protocol implementation
  - `axis_gen2.c/h`: Gen2 AXI stream protocol implementation
  - `axi_version.c/h`: AXI version register access functions
  - `data_dev_top.c/h`: Data device probe/remove (PCI driver wrapper)
- Pattern: All drivers include these via symlinks

**data_dev/**
- Purpose: TID-AIR PCIe driver with optional GPU Direct RDMA support
- Contains:
  - `app/src/`: dmaLoopTest, dmaRead, dmaWrite, test (main applications)
  - `driver/src/`: Symlinks to common drivers + data_dev_top, gpu_async, axis_gen2
  - `driver/Makefile`: Builds datadev.ko module
- Key feature: Supports NVIDIA GPU async transfers when `NVIDIA_DRIVERS` defined
- Device: `/dev/datadev_0` (or other indices)

**rce_stream/**
- Purpose: RCE (Remote Control Engine) AXI Stream DMA driver
- Contains:
  - `app/`: Makefile for building RCE applications
  - `driver/src/`: dma_common, dma_buffer, axis_gen1, axis_gen2, rce_top
- Target: ARM cross-compilation for RCE platform
- Device: `/dev/axi_stream_dma_*`

**rce_memmap/**
- Purpose: RCE Memory Map interface for register access
- Contains:
  - `driver/src/rce_map.c`: Memory mapping, register read/write
- Pattern: Simple char device driver for virtual memory access
- Device: `/dev/rce_memmap`

**rce_hp_buffers/**
- Purpose: RCE High Performance buffer allocation driver
- Contains:
  - `driver/src/rce_hp.c`: Buffer management for firmware DMA
  - `driver/src/rce_top.c`: Platform driver setup
- Pattern: Hardware-specific buffer management

**include/**
- Purpose: Application-facing headers (user-space API)
- Contains:
  - `DmaDriver.h`: DMA API (ioctl commands, data structures)
  - `AxisDriver.h`: AXIS protocol helpers (flag manipulation)
  - `AxiVersion.h`: AXI version register access
  - `DataDriver.h`: Combined interface for all drivers
  - `GpuAsync.h`, `GpuAsyncRegs.h`, `GpuAsyncUser.h`: GPU async support

**scripts/**
- Purpose: Development utilities
- Contains:
  - `check-bar.sh`: BAR validation
  - `filter-clangdb.py`: Clang database filtering
  - `run-clang-tidy.py`: Static analysis runner
  - `uploadTag.py`: GitHub release asset upload

**Yocto/**
- Purpose: Yocto BitBake recipes for embedded builds
- Contains:
  - `recipes-kernel/aximemorymap/`: rce_memmap recipe
  - `recipes-kernel/axistreamdma/`: rce_stream recipe
  - `recipes-tests/axidmasamples/`: Test applications recipe

## Key File Locations

**Entry Points:**
- `Makefile`: Top-level build orchestration (app, driver, rce targets)
- `data_dev/driver/Makefile`: Data device module build
- `rce_stream/driver/Makefile`: RCE stream module build
- `rce_memmap/driver/Makefile`: RCE memmap module build
- `rce_hp_buffers/driver/Makefile`: RCE HP buffers module build

**Configuration:**
- `Makefile` (lines 22-29): Kernel version discovery, RCE directories
- `data_dev/driver/datadev.conf`: Module parameters (cfgTxCount, cfgRxCount, cfgSize)
- `pip_requirements.txt`: Python dependencies for scripts

**Core Logic:**
- `common/driver/dma_common.c`: Core DMA driver infrastructure
- `common/driver/dma_buffer.c`: Buffer pool and queue management
- `data_dev/driver/src/data_dev_top.c`: PCI driver probe/remove
- `rce_stream/driver/src/rce_top.c`: Platform driver probe/remove

**Testing:**
- `common/app/dmaLoopTest.cpp`: Full loopback test with multiple lanes
- `data_dev/app/src/test.cpp`: Basic write test
- `data_dev/app/src/dmaRate.cpp`: Performance rate testing

**Documentation:**
- `README.md`: Project overview and quick start
- `docs/DMA.rst`: User-space API documentation

## Naming Conventions

**Files:**
- Kernel drivers: Lowercase with underscores (dma_common.c, axis_gen1.h)
- Headers: PascalCase with .h extension (DmaDriver.h, AxisDriver.h)
- Applications: PascalCase (dmaLoopTest.cpp, PrbsData.h)
- Makefiles: UPPERCASE (Makefile)

**Directories:**
- Lowercase with underscores (data_dev, rce_stream, common)
- No trailing slashes in references

**Functions:**
- Kernel functions: Prefix with module (Dma_*, AxisG1_*, RceHp_*)
- Inline helpers: Lowercase (axisSetFlags, dmaWrite)
- IOCTL handlers: Dma_Ioctl

**Macros:**
- IOCTL commands: UPPERCASE with underscores (DMA_Get_Buff_Count, AXIS_Read_Ack)
- Error codes: DMA_ERR_* (DMA_ERR_FIFO, DMA_ERR_BUS)
- Configuration: cfg* prefix (cfgTxCount, cfgSize)

## Where to Add New Code

**New Feature (Application):**
- Primary code: `common/app/` (shared across all platforms)
- Tests: Create in `data_dev/app/src/` or `rce_stream/app/`
- Link from data_dev via symlink in `data_dev/app/src/`

**New Driver/Module:**
- Implementation: `common/driver/` (if shared) or `driver_name/driver/` (if unique)
- Headers: `include/` (application-facing) or module-local (driver-facing)
- Makefile: Add to root `Makefile` driver target

**Utilities:**
- Shared helpers: `common/app_lib/` or `scripts/`
- Build integration: Update root `Makefile` or create new target

**Test Files:**
- Unit tests: Create in `data_dev/app/src/` or `rce_stream/app/src/`
- Loopback tests: Use pattern from `common/app/dmaLoopTest.cpp`

**Cross-Platform Support:**
- Add to `common/driver/` for shared functionality
- Use kernel version checks (`#if LINUX_VERSION_CODE >= ...`)
- Use `#ifdef DATA_GPU` for GPU-specific code paths

## Special Directories

**Yocto/**
- Purpose: Yocto embedded build system recipes
- Generated: No (source recipes)
- Committed: Yes
- Key files: `.bb` recipes, `FILES_*` package definitions

**.github/workflows/**
- Purpose: GitHub Actions CI/CD
- Contains: `aes_ci.yml` - builds on Ubuntu, Debian, Rocky Linux, CentOS
- Test targets: C/C++ linter, Sparse static analysis, Clang-Tidy

---

*Structure analysis: 2026-03-25*