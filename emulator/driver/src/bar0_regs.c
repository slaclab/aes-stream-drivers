/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    BAR0 memory allocation, register initialization, and cleanup for the
 *    virtual PCI device emulator.
 *
 *    Allocates 16MB of physically contiguous memory, marks it uncacheable
 *    for ioremap coherency, and pre-populates AXIS Gen2 and AxiVersion
 *    registers so the real datadev driver can probe successfully.
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

#include <linux/mm.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "bar0_regs.h"
#include "gpu_engine.h"     /* EMU_GPU_DATA_BYTES_C — mirrored into BAR0 */

/* System RAM resources in iomem_resource carry IORESOURCE_SYSRAM on every
 * supported kernel.  Defined locally in case a future minimal build config
 * omits the header definition. */
#ifndef IORESOURCE_SYSRAM
#define IORESOURCE_SYSRAM  0x02000000
#endif

/**
 * emu_bar0_alloc - Allocate physically contiguous BAR0 memory
 * @bar: Pointer to emu_bar0 structure to fill in
 *
 * Allocates zeroed pages and marks them PageReserved so the kernel's
 * ioremap "ioremap on RAM" check (__ioremap_check_ram via
 * walk_system_ram_range) skips them.  Pages stay write-back in the
 * direct-map; the driver's ioremap registers its own UC PAT entry on
 * the same physical pages.
 *
 * Return: 0 on success, negative errno on failure.
 */
int emu_bar0_alloc(struct emu_bar0 *bar)
{
   struct page *pages = NULL;
   unsigned int order;
   unsigned long nr_pages, i;

   /* Request the full 16MB first.  On runners whose memory is fragmented
    * (common for long-running VMs), order-12 contiguous allocations can
    * fail with -ENOMEM even when total free memory is ample.  Fall back
    * to progressively smaller orders down to EMU_BAR0_MIN_ORDER, which
    * still covers every register region the driver touches at probe and
    * during normal operation.  Use __GFP_NORETRY | __GFP_NOWARN so the
    * kernel does not log intimidating allocation warnings when a larger
    * order fails and we retry at a smaller one.
    *
    * Do NOT request __GFP_COMP: we need to set PageReserved on every
    * page individually (see below), and SetPageReserved's page-flag
    * policy is PF_NO_COMPOUND. */
   for (order = EMU_BAR0_ORDER; order >= EMU_BAR0_MIN_ORDER; order--) {
      pages = alloc_pages(GFP_KERNEL | __GFP_ZERO |
                          __GFP_NORETRY | __GFP_NOWARN, order);
      if (pages)
         break;
   }
   if (!pages) {
      pr_err("emu: failed to allocate BAR0 memory (min order %u)\n",
             EMU_BAR0_MIN_ORDER);
      return -ENOMEM;
   }

   bar->pages = pages;
   bar->phys  = page_to_phys(pages);
   bar->virt  = page_address(pages);
   bar->size  = (unsigned long)PAGE_SIZE << order;
   nr_pages   = 1UL << order;

   /* Sanity-check: bar->size must never fall below the minimum order size.
    * This catches any future regression where EMU_BAR0_MIN_ORDER is raised
    * above the order that the buddy allocator actually returned. */
   BUG_ON(bar->size < EMU_BAR0_MIN_SIZE);

   if (order < EMU_BAR0_ORDER) {
      pr_warn("emu: BAR0 reduced to %luKB (requested %uKB) due to memory fragmentation\n",
              bar->size / 1024, EMU_BAR0_SIZE / 1024);
   }

   /* Mark every page PageReserved so __ioremap_check_ram (called by the
    * datadev driver's ioremap) skips the "ioremap on RAM" refusal.
    * walk_system_ram_range explicitly skips PageReserved pages.
    *
    * NOTE: We intentionally do NOT call set_memory_uc() here.  On kernel
    * 6.17+, set_memory_uc registers a PAT tracker entry
    * (reserve_ram_pages_type) that causes the driver's later ioremap to
    * fail with -EBUSY in reserve_pfn_range.  The driver registers its own
    * UC PAT entry via ioremap; cache coherency between the WB direct-map
    * (used by emulator-side iowrite32 register pokes) and the UC ioremap
    * (used by the driver) is handled by x86 cache snoop for these small,
    * sparse register writes. */
   for (i = 0; i < nr_pages; i++)
      SetPageReserved(pages + i);

   /* Create a UC (uncacheable) ioremap of the BAR0 physical range.
    * The emulator uses this for all register writes and reads so that
    * register values are immediately visible to the driver's UC ioremap
    * without any cache flush.  PageReserved (set above) lets ioremap
    * succeed on these RAM pages.  The driver's later ioremap creates
    * a second UC mapping of the same physical pages — two UC mappings
    * on the same address are coherent by definition. */
   bar->uc_virt = ioremap(bar->phys, bar->size);
   if (!bar->uc_virt) {
      pr_err("emu: failed to ioremap BAR0 for UC access\n");
      for (i = 0; i < nr_pages; i++)
         ClearPageReserved(pages + i);
      __free_pages(pages, order);
      bar->pages = NULL;
      return -ENOMEM;
   }

   pr_info("emu: BAR0 allocated at phys 0x%llx virt 0x%p uc 0x%p size 0x%lx\n",
           (unsigned long long)bar->phys, bar->virt, bar->uc_virt, bar->size);

   return 0;
}

/**
 * emu_bar0_init_regs - Pre-populate BAR0 registers for driver probe
 * @bar: Pointer to an allocated emu_bar0 structure
 *
 * Writes AXIS Gen2 and AxiVersion register values that the datadev
 * driver reads during DataDev_Probe and AxisG2_Init.  The entire
 * region is already zeroed from __GFP_ZERO, so only non-zero values
 * need explicit writes.
 *
 * All writes go through bar->uc_virt (UC ioremap) so they are
 * immediately visible to the driver's own UC ioremap without any
 * cache flush.
 */
void emu_bar0_init_regs(struct emu_bar0 *bar, u32 max_buffers)
{
   void __iomem *base = bar->uc_virt;

   /* --- AXIS Gen2 registers (at base + EMU_AGEN2_OFF) --- */

   /* enableVer (0x0000): version 3, 128-bit desc, enable bits */
   iowrite32(EMU_REG_ENABLEVER_INIT, base + EMU_AGEN2_OFF + 0x0000);

   /* maxSize (0x0028): 128KB max transfer */
   iowrite32(EMU_REG_MAXSIZE_INIT, base + EMU_AGEN2_OFF + 0x0028);

   /* online (0x002C): device is online */
   iowrite32(EMU_REG_ONLINE_INIT, base + EMU_AGEN2_OFF + 0x002C);

   /* channelCount (0x0034): axiWidth=64 in bits 15:8, channels=1 in bits 7:0 */
   iowrite32(EMU_REG_CHANCOUNT_INIT, base + EMU_AGEN2_OFF + 0x0034);

   /* addrWidth (0x0038): 12 => 1 << 12 = 4096 ring entries */
   iowrite32(EMU_REG_ADDRWIDTH_INIT, base + EMU_AGEN2_OFF + 0x0038);

   /* --- AxiVersion registers (at base + EMU_AVER_OFF) --- */

   /* firmwareVersion (0x0000): emulator version tag */
   iowrite32(EMU_REG_FW_VERSION_INIT, base + EMU_AVER_OFF + 0x0000);

   /* scratchPad (0x0004): emulator build version — changes each compile.
    * The CI harness prints this at insmod to confirm a stale module is not
    * being reused across runs.  Derived from __TIME__ (HH:MM:SS). */
   iowrite32(EMU_BUILD_VERSION, base + EMU_AVER_OFF + 0x0004);

   /* userReset (0x010C): clear */
   iowrite32(0x00000000, base + EMU_AVER_OFF + 0x010C);

   /* --- GPU Async Core V4 registers (at base + EMU_GPU_ASYNC_OFF) ---
    *
    * Only two non-zero values are required so Gpu_Init (in
    * gpu_async.c) takes the V4 path:
    *   - Version (+0x0030) must read 4  => dev->gpuEn = 1
    *   - MaxBuffersV4 (+0x0000, bits 10:0) must be >= 1
    *
    * All other GPU Async registers (write buffer addrs at +0x4000,
    * read buffer addrs at +0x6000, write-detect slots at +0x2000,
    * etc.) are already zero from __GFP_ZERO and behave as passive
    * backing-store memory for driver loads/stores.
    *
    * V1/V2/V3 layouts are intentionally NOT emulated.
    */

   /* MaxBuffersV4 (0x0000, bits 10:0): buffer count driven by the
    * emu_gpu_max_buffers insmod parameter (see emu_main.c). Mask to the
    * V4 register field width [10:0] so an out-of-range insmod cannot
    * write upper bits (defence in depth -- emu_init() already rejects
    * values > 1024 with -EINVAL before reaching this code path). */
   iowrite32(max_buffers & 0x7FF, base + EMU_GPU_ASYNC_OFF + 0x0000);

   /* DmaDataBytes (0x4, bits 23:16): mirrors DMA_AXI_CONFIG_G.DATA_BYTES_C
    * to userspace. rdmaTestEmu / rdmaTest.cu call GpuAsyncCoreRegs::
    * dmaDataBytes() to skip past the in-band AxiWrDesc64_t header before
    * PRBS verify. Must equal EMU_GPU_DATA_BYTES_C (the offset where
    * emu_gpu_rx_tick writes the PRBS payload — gpu_engine.c:254). If left
    * at 0, userspace reads the header word as the first PRBS word and
    * PrbsData::processData reports "Bad size. exp=262148, got=65536". */
   iowrite32((u32)EMU_GPU_DATA_BYTES_C << 16, base + EMU_GPU_ASYNC_OFF + 0x4);

   /* Version (0x0030, bits 7:0): 4 => V4 register layout */
   iowrite32(EMU_GPU_VERSION_INIT, base + EMU_GPU_ASYNC_OFF + 0x0030);

   pr_info("emu: BAR0 GPU Async V4 initialized (version=4, maxBuffers=%u)\n",
           max_buffers & 0x7FF);

   /* Verify init writes are readable back through UC mapping. The
    * Desc128En bit (16) MUST be set: if the driver reads it back as 0,
    * the workqueue is never created and DMA traffic silently drops
    * (see common/driver/axis_gen2.c:579-581). WARN_ON produces a
    * stack-trace-equivalent dmesg message that the CI gate detects.
    */
   {
      uint32_t readback = ioread32(base + EMU_AGEN2_OFF + 0x0000);
      pr_info("emu: BAR0 enableVer readback=0x%08x (expected 0x%08x)\n",
              readback, EMU_REG_ENABLEVER_INIT);
      WARN_ON(readback != EMU_REG_ENABLEVER_INIT);
      WARN_ON((readback & 0x10000) == 0);
   }

   pr_info("emu: BAR0 registers initialized\n");
}

/**
 * emu_bar0_claim_iomem - Make the BAR0 phys range claimable via request_mem_region
 * @bar: Pointer to emu_bar0 structure already populated by emu_bar0_alloc
 *
 * alloc_pages returns pages inside a "System RAM" child of iomem_resource.
 * System RAM carries IORESOURCE_BUSY, and __request_region only recurses
 * into a conflict when it fully contains the request AND is not BUSY, so a
 * consumer driver's request_mem_region(bar->phys, bar->size, ...) fails
 * with -EBUSY.  This produces the observed "Init: Memory in use." failure
 * path in common/driver/dma_common.c:Dma_MapReg for virtual PCI emulation.
 *
 * Structural carving (adjust_resource to shrink System RAM) is not viable
 * because the Kernel code/rodata/data/bss children can sit above our BAR0
 * phys when KASLR randomizes the kernel base high in System RAM, and
 * adjust_resource refuses to shrink across those children.
 *
 * Approach: leave System RAM's extents and children untouched and just
 * clear IORESOURCE_BUSY on the single System RAM slot that contains our
 * BAR0 range for the emulator's lifetime.  This enables __request_region
 * recursion through that slot; consumer drivers' request_mem_region then
 * succeeds by inserting a BUSY child in the free hole where our pages live.
 * We also insert a non-BUSY "emu-bar0" IORESOURCE_MEM child of the slot so
 * /proc/iomem names the window and later release paths can remove it
 * deterministically.
 *
 * The System RAM BUSY flag is restored in emu_bar0_release_iomem.  While
 * the emulator is loaded, request_mem_region on unrelated System RAM
 * addresses would not be blocked at the System RAM level; in practice no
 * mainline driver does that (request_mem_region is for MMIO, not RAM), and
 * this module is only loaded on CI-controlled test hosts.
 *
 * Return: 0 on success, negative errno on failure.
 */
int emu_bar0_claim_iomem(struct emu_bar0 *bar)
{
   struct resource *slot;
   resource_size_t phys = bar->phys;
   resource_size_t size = bar->size;
   resource_size_t end  = phys + size - 1;
   int ret;

   if (!bar->size) {
      pr_err("emu: cannot claim iomem for empty BAR0\n");
      return -EINVAL;
   }

   bar->sysram_slot        = NULL;
   bar->sysram_saved_flags = 0;
   bar->flags_modified     = false;
   bar->bar_inserted       = false;

   /* Walk iomem_resource top-level children to find the System RAM entry
    * that contains our phys range.  alloc_pages guarantees page-backed
    * memory in one such entry (one per NUMA node on multi-socket hosts). */
   for (slot = iomem_resource.child; slot; slot = slot->sibling) {
      if ((slot->flags & IORESOURCE_SYSRAM) == 0)
         continue;
      if (slot->start <= phys && slot->end >= end)
         break;
   }
   if (!slot) {
      pr_err("emu: BAR0 phys 0x%llx..0x%llx not found in any System RAM resource\n",
             (unsigned long long)phys, (unsigned long long)end);
      return -ENXIO;
   }

   bar->sysram_slot        = slot;
   bar->sysram_saved_flags = slot->flags;

   /* Clear IORESOURCE_BUSY on just this slot.  __request_region recursion
    * now descends into the slot; within it, Kernel code/data and other
    * BUSY children still reject overlapping claims, so only the free
    * hole where our pages live becomes claimable. */
   slot->flags &= ~IORESOURCE_BUSY;
   bar->flags_modified = true;

   /* Insert BAR0 window as a non-BUSY named child of the slot so the
    * window shows up in /proc/iomem and the consumer claim has a labeled
    * parent.  Non-BUSY: the consumer driver still needs to insert its
    * own BUSY child via request_mem_region. */
   memset(&bar->bar0_res, 0, sizeof(bar->bar0_res));
   bar->bar0_res.start = phys;
   bar->bar0_res.end   = end;
   bar->bar0_res.name  = "emu-bar0";
   bar->bar0_res.flags = IORESOURCE_MEM;  /* NOT BUSY */
   bar->bar0_res.desc  = IORES_DESC_NONE;
   ret = insert_resource(&iomem_resource, &bar->bar0_res);
   if (ret) {
      pr_err("emu: cannot insert BAR0 window 0x%llx..0x%llx: %d\n",
             (unsigned long long)phys, (unsigned long long)end, ret);
      slot->flags = bar->sysram_saved_flags;
      bar->flags_modified = false;
      bar->sysram_slot = NULL;
      return ret;
   }
   bar->bar_inserted = true;

   pr_info("emu: BAR0 iomem window claimed at 0x%llx..0x%llx (System RAM slot [0x%llx..0x%llx] unbusied)\n",
           (unsigned long long)phys,
           (unsigned long long)end,
           (unsigned long long)slot->start,
           (unsigned long long)slot->end);
   return 0;
}

/**
 * emu_bar0_release_iomem - Undo emu_bar0_claim_iomem
 * @bar: Pointer to emu_bar0 structure whose claim state is to be reversed
 *
 * Removes the BAR0 window and restores IORESOURCE_BUSY on the System RAM
 * slot.  Safe to call even when the claim partially failed -- per-step
 * flags track which mutations actually executed.
 */
void emu_bar0_release_iomem(struct emu_bar0 *bar)
{
   if (bar->bar_inserted) {
      remove_resource(&bar->bar0_res);
      bar->bar_inserted = false;
   }

   if (bar->flags_modified && bar->sysram_slot) {
      bar->sysram_slot->flags = bar->sysram_saved_flags;
      bar->flags_modified     = false;
   }

   bar->sysram_slot        = NULL;
   bar->sysram_saved_flags = 0;
}

/**
 * emu_bar0_free - Release BAR0 memory
 * @bar: Pointer to emu_bar0 structure to free
 *
 * Clears PageReserved and frees pages back to the page allocator.
 */
void emu_bar0_free(struct emu_bar0 *bar)
{
   unsigned long nr_pages, i;
   unsigned int order;

   if (!bar->pages)
      return;

   if (bar->uc_virt) {
      iounmap(bar->uc_virt);
      bar->uc_virt = NULL;
   }

   nr_pages = bar->size / PAGE_SIZE;
   order    = get_order(bar->size);

   for (i = 0; i < nr_pages; i++)
      ClearPageReserved(bar->pages + i);

   __free_pages(bar->pages, order);

   bar->pages = NULL;
   bar->virt  = NULL;
   bar->phys  = 0;

   pr_info("emu: BAR0 memory freed\n");
}
