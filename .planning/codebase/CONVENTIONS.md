# Coding Conventions

**Analysis Date:** 2026-03-26

## Naming Patterns

### Files

- Kernel driver source: `snake_case.c` / `snake_case.h` (e.g., `dma_common.c`, `dma_buffer.h`, `axis_gen2.c`)
- Public API headers (shared kernel+userspace): `PascalCase.h` in `include/` (e.g., `DmaDriver.h`, `AxisDriver.h`, `AxiVersion.h`, `GpuAsync.h`)
- Application source: `camelCase.cpp` in `app/` directories (e.g., `dmaRead.cpp`, `dmaWrite.cpp`, `dmaLoopTest.cpp`)
- Test apps: `camelCase.cpp` or `test.cpp` in `data_dev/app/src/`

### Functions

Function naming follows a strict `Subsystem_Verb` or `subsystem_verb` pattern based on visibility:

**Exported driver functions (PascalCase prefix):**
- `Dma_*` — common DMA layer (`Dma_Init`, `Dma_Open`, `Dma_Read`, `Dma_Write`, `Dma_Clean`, `Dma_Ioctl`, `Dma_MapReg`)
- `AxisG1_*` — Gen1 hardware-specific (`AxisG1_Irq`, `AxisG1_Init`, `AxisG1_SendBuffer`)
- `AxisG2_*` — Gen2 hardware-specific (`AxisG2_Irq`, `AxisG2_Init`, `AxisG2_Process`, `AxisG2_SendBuffer`)
- `Gpu_*` — GPU async functions (`Gpu_Init`, `Gpu_Clear`)
- `DataDev_*` — top-level module (`DataDev_Init`, `DataDev_Exit`, `DataDev_Probe`, `DataDev_Remove`)
- `AxiVersion_*` — version register (`AxiVersion_Get`, `AxiVersion_Read`)

**Internal/utility functions (lower_snake_case):**
- `dmaAllocBuffers`, `dmaFreeBuffers`, `dmaQueueInit`, `dmaQueuePush`, `dmaQueuePop`
- `dmaFindBuffer`, `dmaGetBuffer`, `dmaRetBufferIrq`, `dmaRxBuffer`
- `dmaSortBuffers`, `dmaBufferToHw`, `dmaBufferFromHw`

The rule: internal buffer/queue utilities are lowercase; the public driver interface (`Dma_*`) and hardware backends (`AxisG2_*`) are PascalCase-prefixed.

**IRQ-safe variants** carry an `Irq` suffix: `dmaQueuePushIrq`, `dmaQueuePopIrq`, `dmaRetBufferIrq`, `dmaRxBufferIrq`.

**List-bulk variants** carry a `List` suffix: `dmaQueuePushList`, `dmaQueuePopList`, `dmaFreeBuffersList`.

### Structures and Types

- All structs use PascalCase: `struct DmaDevice`, `struct DmaBuffer`, `struct DmaDesc`, `struct DmaQueue`, `struct DmaBufferList`
- Hardware register maps use a compound name: `struct AxisG2Reg`, `struct AxisG1Reg`
- Hardware-private data structs: `struct AxisG2Data`, `struct AxisG2Return`
- Function pointer tables: `struct hardware_functions` (snake_case, one exception to the rule)
- Typedef not used — all struct references include the `struct` keyword

### Variables

- Local variables: short `camelCase` (`buff`, `desc`, `dev`, `cnt`, `ret`, `iflags`, `rdData`)
- Global variables: `g` prefix (`gDmaDevCount`, `gCl`, `gDmaDevices`)
- Module parameters: `cfg` prefix (`cfgTxCount`, `cfgRxCount`, `cfgSize`, `cfgMode`, `cfgIrqHold`, `cfgDebug`)
- Loop indices: single-letter or short (`x`, `sl`, `sli`)
- IRQ save variable: `iflags` (by convention)

### Macros and Constants

- All-caps `UPPER_SNAKE_CASE`: `DMA_MAX_DEST`, `BUFF_COHERENT`, `BUFF_STREAM`, `BUFF_ARM_ACP`, `BUFFERS_PER_LIST`
- Ioctl command codes: `DMA_Get_Buff_Count`, `DMA_Set_Mask`, `AXIS_Read_Ack` (mixed PascalCase words separated by underscores)
- Error bit flags: `DMA_ERR_FIFO`, `DMA_ERR_LEN`, `DMA_ERR_MAX`, `DMA_ERR_BUS`
- Header guards: `__FILENAME_H__` double-underscore wrapping (e.g., `__DMA_COMMON_H__`, `__DMA_BUFFER_H__`)
- Compile-time conditional: `DMA_IN_KERNEL` to gate kernel-only types in shared headers

### File Header Guards (C)

```c
#ifndef __DMA_COMMON_H__
#define __DMA_COMMON_H__
// ...
#endif  // __DMA_COMMON_H__
```

## Code Style

### Indentation

- 3 spaces (not tabs, not 4 spaces) — enforced in `.editorconfig` for all `.c`, `.cpp`, `.h` files
- CI enforces: no trailing whitespace, no tab characters (checked via `grep` in `aes_ci.yml`)

### Line Length

- `CPPLINT.cfg` sets `linelength=250` — effectively unlimited for driver code
- No hard line-length discipline observed in practice

### Linting Tools

- **cpplint** — run on all `*.h`, `*.cpp`, `*.c` files via CI (`find . -name '*.h' ...| xargs cpplint`)
  - Config: `CPPLINT.cfg` — disables `legal/copyright`, `build/header_guard`, `readability/casting`, `runtime/int`, `runtime/threadsafe_fn`, `build/include_subdir`, `whitespace/indent`, `build/include_what_you_use`
- **clang-tidy** — run on `debian:experimental` builds only via `scripts/run-clang-tidy.py`
  - Config: `.clang-tidy` enables `bugprone-*` and `performance-*` checks with `WarningsAsErrors: true`
  - Suppressed: `bugprone-easily-swappable-parameters`, `bugprone-assignment-in-if-condition`, `performance-no-int-to-ptr`, `cert-err33-c`, others
- **sparse** — run on `debian:experimental` builds: `make driver C=2 CF=-Wsparse-error`
- **clangd** — LSP config in `.clangd` strips `-m*` and `-f*` flags for IDE use

## Comment and Documentation Style

### File Header Block

Every source file begins with a standardized block comment:

```c
/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    [Multi-line description of the file's purpose]
 * ----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * ...
 * ----------------------------------------------------------------------------
**/
```

### Function Documentation

All exported functions use kernel-doc style with `@param:` and `Return:` tags:

```c
/**
 * Dma_MapReg - Maps the address space in the buffer for DMA operations
 * @dev: pointer to the DmaDevice structure
 *
 * This function attempts to map the device's register space into memory,
 * using ioremap or ioremap_nocache depending on the kernel version.
 * ...
 *
 * Return: 0 on success, -1 on failure.
 */
int Dma_MapReg(struct DmaDevice *dev) {
```

### Struct Documentation

Structs use kernel-doc format with `@field:` descriptions for each member, placed above the struct definition:

```c
/**
 * struct DmaDevice - Represents a DMA-capable device.
 * @baseAddr: Base physical address of the device's memory-mapped I/O region.
 * @baseSize: Size of the memory-mapped I/O region.
 * ...
 */
struct DmaDevice {
```

### Inline Comments

- Block actions are commented with a short phrase above the code block
- Inline `//` comments used for register offset annotations inside struct definitions:
  ```c
  uint32_t enableVer;        // 0x0000
  uint32_t intEnable;        // 0x0004
  ```
- `///< Trailing` doc-comment style used inside static struct initializers

## Error Handling Patterns

### Return Values

- Kernel functions return `0` on success, `-1` (not `-ERRNO`) for driver-internal failures, or a proper `-ERRNO` at the PCI probe boundary
- Examples:
  - `Dma_MapReg` returns `0` / `-1`
  - `Dma_Init` returns `0` / `-1`
  - `DataDev_Probe` returns `0`, `-EINVAL`, `-ENOMEM`
  - `AxiVersion_Get` returns `0` / `-1`
- `copy_to_user` / `copy_from_user` failures: log with `dev_warn`, return `-1`
- Allocation failures: log with `dev_err` or `dev_warn`, return `-ENOMEM` or `-1`

### Goto-based Cleanup (Init Paths)

`Dma_Init` and `DataDev_Probe` use the standard Linux goto-cleanup pattern. Labels are prefixed `cleanup_` describing what was allocated at that level:

```c
goto cleanup_alloc_chrdev_region;
goto cleanup_cdev_add;
goto cleanup_class_create;
goto cleanup_device_create;
goto cleanup_proc_create_data;
goto cleanup_dma_mapreg;
goto cleanup_tx_buffers;
goto cleanup_dma_queue;
goto cleanup_card_clear;
```

Each label undoes exactly one allocation in reverse order.

### Null Checks

- All `kzalloc`/`kmalloc` results checked immediately with `if (!ptr)` or `if (ptr == NULL)`
- Pointer parameters checked before use: `if (dev->hwFunc)` before calling function pointers
- `NULL` guard on `gCl` global class pointer before `device_destroy`

### Logging Levels

```c
dev_err(dev->device, "...");   // fatal errors, allocation failures
dev_warn(dev->device, "...");  // recoverable issues, copy failures, error frames
dev_info(dev->device, "...");  // normal init/cleanup lifecycle messages
pr_err("%s: ...", MOD_NAME);   // module-level errors before device is set up
pr_info("%s: ...", MOD_NAME);  // module init/exit messages
```

Debug-gated messages:
```c
if (dev->debug > 0)
   dev_info(dev->device, "...");
```

## Memory Management Patterns

### Kernel Allocations

- Use `kzalloc` (zero-initializing) for all struct/buffer allocations; `kmalloc` avoided
- Follow with `memset` only when explicitly needed (legacy pattern in a few places)
- Every `kzalloc` is matched with a `kfree` in the corresponding cleanup path
- Buffer descriptor structs: `kzalloc(sizeof(struct DmaDesc), GFP_KERNEL)` in `Dma_Open`, freed in `Dma_Release`

### DMA Buffer Allocations

Three buffer modes, selected by `cfgMode`:
- `BUFF_COHERENT` — `dma_alloc_coherent()` / `dma_free_coherent()` (most common, avoids cache issues)
- `BUFF_STREAM` — `kmalloc()` + `dma_map_single()` / `dma_unmap_single()` + `kfree()`
- `BUFF_ARM_ACP` — `kmalloc()` + physical address for ARM coherent port

Buffer lists use a two-level indexed structure: `DmaBufferList.indexed[subList][offset]` to handle up to `BUFFERS_PER_LIST` (100,000) buffers per sub-list. A parallel `sorted` array enables binary search by DMA handle.

### Teardown Order

`Dma_Clean` always:
1. Disables interrupts on hardware
2. Frees IRQ
3. Calls `hwFunc->clear(dev)`
4. Frees RX buffers, TX buffers
5. Frees TX queue
6. Unmaps registers
7. Removes proc entry, deletes cdev
8. Destroys device and optionally the device class
9. Zeroes the `DmaDevice` struct: `memset(dev, 0, sizeof(struct DmaDevice))`

## Locking Conventions

Three spinlocks in `struct DmaDevice`:

| Lock | Field | Protects |
|------|-------|----------|
| `writeHwLock` | `dev->writeHwLock` | Hardware register write operations |
| `commandLock` | `dev->commandLock` | Ioctl command serialization |
| `maskLock` | `dev->maskLock` | `desc[]` pointer array and `destMask` updates |

Each `struct DmaQueue` carries its own `spinlock_t lock` for queue push/pop.

**IRQ-safe locking:** Use `spin_lock_irqsave` / `spin_unlock_irqrestore` with a local `unsigned long iflags` when acquiring `maskLock` from process context. IRQ-context callers use the `*Irq` variants of queue functions (which use `spin_lock_irqsave` internally).

**Pattern example:**
```c
unsigned long iflags;
spin_lock_irqsave(&dev->maskLock, iflags);
// modify dev->desc[] or destMask
spin_unlock_irqrestore(&dev->maskLock, iflags);
```

No mutexes are used in the driver core — only spinlocks, consistent with interrupt-context requirements.

## Import / Include Organization

Kernel driver files follow this order:

1. Hardware-specific local headers (e.g., `#include <axis_gen2.h>`)
2. Shared public API headers (e.g., `#include <AxisDriver.h>`, `#include <DmaDriver.h>`)
3. Linux kernel headers (`#include <linux/module.h>`, `#include <linux/slab.h>`, etc.)

Application files:
1. POSIX / C standard (`<sys/types.h>`, `<unistd.h>`, `<stdio.h>`)
2. Project API headers (`<AxisDriver.h>`, `<DmaDriver.h>`, `<PrbsData.h>`)
3. C++ std (`<iostream>`)

## Version Compatibility Guards

Extensive use of `LINUX_VERSION_CODE` comparisons to handle kernel API changes:

```c
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
   // use proc_ops
#else
   // use file_operations for proc
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0) || \
    (defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9, 4))
   gCl = class_create(dev->devName);
#else
   gCl = class_create(THIS_MODULE, dev->devName);
#endif
```

The `MAYBE_UNUSED` macro wraps `__attribute__((unused))` for GCC portability.

---

*Convention analysis: 2026-03-26*
