/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    BAR0 memory layout constants, register initialization values, and
 *    function prototypes for the virtual PCI device emulator.
 *
 *    The emulator allocates a 16MB physically contiguous region to serve as
 *    BAR0.  Region offsets match data_dev_top.h so the real datadev driver
 *    can probe and initialize against emulated registers.
 * ----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the aes_stream_drivers package, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 * ----------------------------------------------------------------------------
**/

#ifndef __EMU_BAR0_REGS_H__
#define __EMU_BAR0_REGS_H__

#include <linux/types.h>
#include <linux/ioport.h>

/* ----------------------------------------------------------------
 * BAR0 region constants -- must match data_dev_top.h
 * ---------------------------------------------------------------- */

#define EMU_BAR0_SIZE       0x01000000  /* 16MB total BAR0 */
#define EMU_BAR0_ORDER      12          /* get_order(16MB) = 12 */

/* Minimum fallback order when the buddy allocator cannot satisfy the full
 * 16MB request due to fragmentation (observed on long-running GH Actions
 * runners).  Order 6 = 256KB covers the AGEN2/PHY/AVER/GPU_ASYNC register
 * regions the datadev driver actively accesses (highest offset = 0x30000).
 * The optional USER region (starts at 0x00800000) is not emulated in the
 * fallback path; CI tests must not mmap/ioctl into the USER region when a
 * fallback allocation is in effect. */
#define EMU_BAR0_MIN_ORDER  6

/* Minimum BAR0 size derived from EMU_BAR0_MIN_ORDER.  Used in a BUG_ON
 * assertion in emu_bar0_alloc() to catch any future regression where the
 * minimum order constant is misconfigured. */
#define EMU_BAR0_MIN_SIZE   ((unsigned long)PAGE_SIZE << EMU_BAR0_MIN_ORDER)

/* Region offsets */
#define EMU_AGEN2_OFF       0x00000000  /* AXIS Gen2 DMA engine */
#define EMU_AGEN2_SIZE      0x00010000
#define EMU_PHY_OFF         0x00010000  /* PCIe PHY */
#define EMU_PHY_SIZE        0x00010000
#define EMU_AVER_OFF        0x00020000  /* AxiVersion */
#define EMU_AVER_SIZE       0x00010000
#define EMU_GPU_ASYNC_OFF   0x00028000  /* GPU Async Core */
#define EMU_GPU_ASYNC_SIZE  0x00008000
#define EMU_USER_OFF        0x00800000  /* User Space */
#define EMU_USER_SIZE       0x00800000

/* ----------------------------------------------------------------
 * AXIS Gen2 initial register values (offsets relative to AGEN2)
 * ---------------------------------------------------------------- */

/*
 * enableVer (offset 0x0000): matches AxiStreamDmaV2Desc VHDL field layout.
 *
 *   bits  7:0  -- enable  (R/W in VHDL; only bit 0 is decoded)
 *   bits 15:8  -- enableCnt (R/O; counts enable 0->1 transitions)
 *   bit  16    -- Desc128En (R/O constant, always 1)
 *   bits 31:24 -- version number (R/O constant)
 *
 * The driver writes this register as a single word (writel(0x1) to enable,
 * writel(0x0) to disable). On real silicon, axiSlaveRegisterR discards
 * writes to the R/O fields; the R/O bits hold their constant (version,
 * Desc128En) or internally-maintained (enableCnt) values.
 *
 * The emulator has no AXI decoder, so the DMA poll thread enforces the
 * same R/O contract: every poll cycle it reads the register, preserves
 * bit 0 (the driver's enable write), increments enableCnt on a 0->1
 * edge of bit 0, and rewrites the word with version+Desc128En reasserted.
 * See emu_enforce_enablever_ro() in dma_engine.c.
 *
 * Desc128En is hard-wired to 1 in the emulator; 64-bit descriptor mode is
 * not emulated and is not a supported driver configuration.
 */
#define EMU_REG_ENABLEVER_VERSION    0x03U           /* bits 31:24 */
#define EMU_REG_ENABLEVER_DESC128    (1U << 16)      /* bit 16        */
#define EMU_REG_ENABLEVER_ENABLE_BIT (1U << 0)       /* bit 0 (R/W)   */
#define EMU_REG_ENABLEVER_ENABLE_MASK 0xFFU          /* bits 7:0      */
#define EMU_REG_ENABLEVER_CNT_SHIFT  8               /* bits 15:8     */
#define EMU_REG_ENABLEVER_CNT_MASK   (0xFFU << EMU_REG_ENABLEVER_CNT_SHIFT)
#define EMU_REG_ENABLEVER_RO_MASK \
   (EMU_REG_ENABLEVER_DESC128 | \
    ((u32)EMU_REG_ENABLEVER_VERSION << 24) | \
    EMU_REG_ENABLEVER_CNT_MASK)

/* Initial register value: version + Desc128En, enable=0, enableCnt=0.
 * The driver will transition bit 0 to 1 at its first AxisG2_Enable, which
 * drives the first increment of enableCnt. */
#define EMU_REG_ENABLEVER_INIT \
   (((u32)EMU_REG_ENABLEVER_VERSION << 24) | EMU_REG_ENABLEVER_DESC128)

/* maxSize (offset 0x0028): 128KB max transfer */
#define EMU_REG_MAXSIZE_INIT      0x00020000

/* online (offset 0x002C): device is online */
#define EMU_REG_ONLINE_INIT       0x00000001

/*
 * channelCount (offset 0x0034):
 *   bits 15:8 = axiWidth (64)
 *   bits 7:0  = channel count (1)
 */
#define EMU_REG_CHANCOUNT_INIT    0x00004001

/*
 * addrWidth (offset 0x0038):
 *   log2 of DMA descriptor ring size.  1 << N = ring entries.
 *   NOT the DMA address width (that is axiWidth in channelCount bits 15:8).
 *   AxisG2_Init computes: hwData->addrCount = (1 << readl(&reg->addrWidth));
 *
 *   Value 1 yields 2 ring entries.  The driver can have at most 1 TX
 *   descriptor in-flight (hwRdBuffCnt < addrCount-1 = 1), which
 *   serializes readFifo writes — only one writel() burst per
 *   AxisG2_Process call — preventing the shadow-register overwrite
 *   race.  Throughput is limited to ~1000 TX/s (one per emulator poll
 *   cycle) which is sufficient for CI DMA loopback validation.
 */
#define EMU_REG_ADDRWIDTH_INIT    0x00000001

/* ----------------------------------------------------------------
 * AxiVersion initial register values (offsets relative to AVER)
 * ---------------------------------------------------------------- */

/* firmwareVersion (offset 0x0000): emulator version tag */
#define EMU_REG_FW_VERSION_INIT   0x01000000

/* Build version stamp written to scratchPad (offset 0x0004).
 * Changes every recompile so the CI harness can detect stale modules.
 * Passed via -DEMU_BUILD_VERSION=N from the Makefile. */
#ifndef EMU_BUILD_VERSION
#define EMU_BUILD_VERSION  0
#endif

/* DataGpuEn (offset 0x428 = UserValues[10], bit 0): mirrors the firmware
 * AxiVersion.UserValues(10)(0) = DATAGPU_EN_G generic.  The GPU-build
 * datadev driver (data_dev_top.c, #ifdef DATA_GPU) gates Gpu_Init on this
 * bit reading 1, so the emulator must assert it to present a GPU-capable
 * device. */
#define EMU_REG_DATAGPU_EN_OFF    0x428
#define EMU_REG_DATAGPU_EN_INIT   0x00000001

/* ----------------------------------------------------------------
 * GPU Async Core V5 initial register values (offsets relative to
 * EMU_GPU_ASYNC_OFF = 0x00028000)
 *
 * Only two registers must be non-zero at init.  The remaining 32KB
 * of the GPU Async region (0x28000..0x2FFFF) is plain R/W backing
 * store memory (already zeroed by __GFP_ZERO in emu_bar0_alloc);
 * the driver's Gpu_AddNvidia / Gpu_RemNvidia / Gpu_SetWriteEn paths
 * write into and read from that region as ordinary memory.
 *
 * The V5 layout matches V4 (axi-pcie-core v6.8.1, AxiPcieGpuAsyncControl.vhd)
 * plus the new write/read "active" readback at 0x44 (see gpu_engine.c).
 * V1/V2/V3 are not supported going forward.
 * ---------------------------------------------------------------- */

/*
 * Version register (offset 0x0030, bits 7:0):
 *   Must be 5 so Gpu_Init takes the V5 teardown path (spin on the 0x44
 *   write/read active readback) and sets dev->gpuEn = 1. gpu_async.c:47
 *   reads this via readGpuAsyncReg(..., &GpuAsyncReg_Version).
 */
#define EMU_GPU_VERSION_INIT      0x00000005

/*
 * MaxBuffersV4 register (offset 0x0000, bits 10:0):
 *   Upper-limit count used by Gpu_AddNvidia capacity check and returned
 *   by the GPU_Get_Max_Buffers ioctl.  Four buffers is enough to cover
 *   positive + negative ioctl test cases without excessive test state.
 */
#define EMU_GPU_MAXBUF_INIT       0x00000004

/* ----------------------------------------------------------------
 * BAR0 state tracking structure
 * ---------------------------------------------------------------- */

struct emu_bar0 {
   struct page *pages;       /* allocated compound page */
   phys_addr_t phys;         /* physical address of BAR0 */
   void *virt;               /* kernel virtual address (WB direct-map) */
   void __iomem *uc_virt;    /* uncacheable ioremap (for register coherency) */
   unsigned long size;       /* BAR0 size in bytes */

   /* iomem_resource claim state (populated by emu_bar0_claim_iomem).
    *
    * alloc_pages returns pages inside a "System RAM" child of iomem_resource
    * which has IORESOURCE_BUSY set.  A consumer driver's request_mem_region
    * walks iomem_resource and will only recurse into a conflict that fully
    * contains the request AND is not IORESOURCE_BUSY.  System RAM satisfies
    * "fully contains" but is BUSY, so recursion stops and request fails with
    * "Memory in use.".
    *
    * Carving System RAM (via adjust_resource) only works when no System RAM
    * child (Kernel code/data/bss, crashkernel, etc.) extends past the carve
    * boundaries.  On KASLR-randomized kernels the kernel image can sit well
    * above our BAR0 phys address, so carving fails.
    *
    * To make request_mem_region succeed without structural manipulation of
    * the kernel's System RAM layout, we:
    *   1. Temporarily clear IORESOURCE_BUSY on the System RAM slot that
    *      contains our BAR0 phys range.  This lets __request_region recurse
    *      into System RAM during consumer request_mem_region calls.  The
    *      child tree of System RAM still carries BUSY flags on every claimed
    *      region (Kernel code, Kernel data, reserved zones, etc.), so the
    *      "not busy" window is exactly the free pages our alloc_pages hit.
    *   2. Insert a non-BUSY "emu-bar0" IORESOURCE_MEM resource for the BAR0
    *      phys range as a child of System RAM, making the window visible in
    *      /proc/iomem and giving consumer claims a well-named parent.
    * Both mutations are reversed in emu_bar0_release_iomem. */
   struct resource  bar0_res;        /* inserted BAR0 window, non-BUSY */
   struct resource *sysram_slot;     /* System RAM resource containing BAR0 */
   unsigned long    sysram_saved_flags; /* original flags of sysram_slot */
   bool             bar_inserted;
   bool             flags_modified;
};

/* ----------------------------------------------------------------
 * Function prototypes
 * ---------------------------------------------------------------- */

int emu_bar0_alloc(struct emu_bar0 *bar);
int emu_bar0_claim_iomem(struct emu_bar0 *bar);
void emu_bar0_release_iomem(struct emu_bar0 *bar);
void emu_bar0_init_regs(struct emu_bar0 *bar, u32 max_buffers);
void emu_bar0_free(struct emu_bar0 *bar);

#endif  /* __EMU_BAR0_REGS_H__ */
