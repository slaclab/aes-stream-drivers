/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    Stub implementations of the five nvidia_p2p_* symbols that the real
 *    datadev driver's gpu_async.c calls when compiled with DATA_GPU. Each
 *    function returns a fake-but-valid kernel allocation so the driver can
 *    exercise its ioctl code paths without requiring an NVIDIA GPU or the
 *    real NVIDIA kernel driver.
 *
 *    Backing memory for each nvidia_p2p_get_pages call is an
 *    alloc_pages(GFP_KERNEL|__GFP_ZERO, get_order(length)) region owned by
 *    emu_gpu_addr_table.c. The translation from the fake_dma opaque key
 *    (stored in BAR0) to the kernel virtual address happens via
 *    emu_gpu_addr_lookup(fake_dma), exported as EXPORT_SYMBOL_GPL for the
 *    datadev_emulator.ko consumer.
 * ----------------------------------------------------------------------------
**/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/mmap_lock.h>
#include <linux/sched.h>
#include "nv-p2p.h"
#include "emu_gpu_addr_table.h"
#include "emu_gpu_addr_table_priv.h"

int nvidia_p2p_get_pages(uint64_t p2p_token, uint32_t va_space,
                         uint64_t virtual_address, uint64_t length,
                         struct nvidia_p2p_page_table **page_table,
                         void (*free_callback)(void *data), void *data)
{
   struct nvidia_p2p_page_table *pt;
   struct emu_gpu_addr_entry *entry = NULL;
   bool is_stub = false;
   u32 buf_id = 0;
   struct vm_area_struct *vma;
   int ret;

   (void)p2p_token;
   (void)va_space;
   (void)free_callback;
   (void)data;

   if (!page_table)
      return -EINVAL;
   if (length == 0)
      return -EINVAL;

   /* VMA-reuse path: if virtual_address lies inside a
    * /dev/nvidia_p2p_stub_mem mmap, reuse its existing entry so the
    * user-VA and the emulator-visible kva point at the same physical
    * pages. The fallback path (non-stub VMA, or virtual_address==0)
    * preserves the dmaGpuIoctlTest smoke path which passes no real
    * user VA.
    *
    * mmap_read_lock pins vma->vm_file against concurrent munmap on
    * another thread of current. All VMA-derived data (buf_id, the
    * stub-match boolean) is copied into locals before the unlock;
    * `vma` is not dereferenced after mmap_read_unlock. */
   if (virtual_address && current && current->mm) {
      mmap_read_lock(current->mm);
      vma = find_vma(current->mm, virtual_address);
      if (vma && vma->vm_start <= virtual_address &&
          emu_gpu_addr_is_stub_vma(vma)) {
         is_stub = true;
         buf_id  = (u32)vma->vm_pgoff;
      }
      mmap_read_unlock(current->mm);
   }

   if (is_stub) {
      /* Reuse path: find_by_idx bumps refcount (driver-holder);
       * symmetric with the alloc_and_register path which also starts
       * refcount at 1 for the driver-holder. */
      entry = emu_gpu_addr_table_find_by_idx(buf_id);
      if (!entry)
         return -EINVAL;                               /* reserved then freed */
      if (length > entry->size) {
         emu_gpu_addr_table_release(entry);             /* drop the bump */
         return -EINVAL;
      }
   } else {
      /* Fallback: fresh alloc_pages-backed entry (existing behavior). */
      ret = emu_gpu_addr_table_alloc_and_register(length, &entry);
      if (ret)
         return ret;
   }

   pt = kzalloc(sizeof(*pt), GFP_KERNEL);
   if (!pt) {
      emu_gpu_addr_table_release(entry);               /* drops driver-holder */
      return -ENOMEM;
   }

   pt->version   = NVIDIA_P2P_PAGE_TABLE_VERSION;
   pt->page_size = NVIDIA_P2P_PAGE_SIZE_64KB;
   pt->entries   = (uint32_t)(length / STUB_PAGE_SIZE);
   if (pt->entries == 0)
      pt->entries = 1;
   pt->pages    = NULL;
   /* Identical stash for both paths: nvidia_p2p_free_page_table at
    * lines 160-184 reads `pt->gpu_uuid` as the opaque entry pointer
    * and calls emu_gpu_addr_table_release on it, so refcount
    * discipline is identical regardless of which path allocated. */
   pt->gpu_uuid = (uint8_t *)entry;
   pt->flags    = 0;

   *page_table = pt;
   return 0;
}
EXPORT_SYMBOL(nvidia_p2p_get_pages);

int nvidia_p2p_put_pages(uint64_t p2p_token, uint32_t va_space,
                         uint64_t virtual_address,
                         struct nvidia_p2p_page_table *page_table)
{
   (void)p2p_token;
   (void)va_space;
   (void)virtual_address;
   (void)page_table;
   /* No-op: real put_pages is an alternate release path; stub uses
    * free_page_table exclusively. Exporting the symbol keeps linkage
    * intact for any future consumer that calls put_pages directly. */
   return 0;
}
EXPORT_SYMBOL(nvidia_p2p_put_pages);

int nvidia_p2p_dma_map_pages(struct pci_dev *peer,
                             struct nvidia_p2p_page_table *page_table,
                             struct nvidia_p2p_dma_mapping **dma_mapping)
{
   struct nvidia_p2p_dma_mapping *m;
   struct emu_gpu_addr_entry *entry;
   uint32_t i;

   if (!page_table || !dma_mapping)
      return -EINVAL;
   if (page_table->entries == 0)
      return -EINVAL;

   entry = (struct emu_gpu_addr_entry *)page_table->gpu_uuid;
   if (!entry)
      return -EINVAL;

   m = kzalloc(sizeof(*m), GFP_KERNEL);
   if (!m)
      return -ENOMEM;

   m->version        = NVIDIA_P2P_DMA_MAPPING_VERSION;
   m->page_size_type = NVIDIA_P2P_PAGE_SIZE_64KB;
   m->entries        = page_table->entries;
   m->private        = entry;   /* back-pointer for quick unmap (Open Q2) */
   m->pci_dev        = peer;

   m->dma_addresses = kcalloc(m->entries, sizeof(uint64_t), GFP_KERNEL);
   if (!m->dma_addresses) {
      kfree(m);
      return -ENOMEM;
   }

   /* Contiguity contract: every buffer has a UNIQUE fake_dma_base
    * and dma_addresses[i] are spaced 64KB apart so gpu_async.c:218-223
    * counts them as a single contiguous run. The emulator reads
    * dma_addresses[0] from BAR0 and passes it to emu_gpu_addr_lookup. */
   for (i = 0; i < m->entries; i++)
      m->dma_addresses[i] = entry->fake_dma +
                            ((uint64_t)i * STUB_PAGE_SIZE);

   *dma_mapping = m;
   return 0;
}
EXPORT_SYMBOL(nvidia_p2p_dma_map_pages);

int nvidia_p2p_dma_unmap_pages(struct pci_dev *peer,
                               struct nvidia_p2p_page_table *page_table,
                               struct nvidia_p2p_dma_mapping *dma_mapping)
{
   (void)peer;
   (void)page_table;

   if (!dma_mapping)
      return -EINVAL;

   kfree(dma_mapping->dma_addresses);
   kfree(dma_mapping);
   return 0;
}
EXPORT_SYMBOL(nvidia_p2p_dma_unmap_pages);

int nvidia_p2p_free_page_table(struct nvidia_p2p_page_table *page_table)
{
   struct emu_gpu_addr_entry *entry;

   if (!page_table)
      return -EINVAL;

   /* Retrieve and release the stashed entry (drops driver-holder
    * refcount; hash_del_rcu + synchronize_rcu + __free_pages happen
    * inside emu_gpu_addr_table_release when refcount hits 0). If this
    * was the last entry, the drain callback fires here and the
    * emulator symbol_puts its pin on nvidia_p2p_stub. */
   entry = (struct emu_gpu_addr_entry *)page_table->gpu_uuid;
   page_table->gpu_uuid = NULL;

   /* Free pt BEFORE calling release: the drain-cb invocation (via
    * release -> stub_try_drain) may trigger emulator symbol_put which
    * must not race with any in-flight pt dereference. */
   kfree(page_table);

   if (entry)
      emu_gpu_addr_table_release(entry);

   return 0;
}
EXPORT_SYMBOL(nvidia_p2p_free_page_table);

/* Read-only sysfs readout of EMU_BUILD_VERSION macro so
 * tests/test_gpu_dma_loopback.sh can assert the running .ko matches the
 * freshly-built .build_version sideband file (stale-module gate). */
static unsigned int emu_build_version = EMU_BUILD_VERSION;
module_param_named(build_version, emu_build_version, uint, 0444);
MODULE_PARM_DESC(build_version,
                 "Read-only EMU_BUILD_VERSION stamp (seconds since epoch at build time)");

static int __init nvidia_p2p_stub_init(void)
{
   int ret;

   pr_info("nvidia_p2p_stub: build_version=%u\n", EMU_BUILD_VERSION);

   ret = emu_gpu_addr_table_init();
   if (ret) {
      pr_err("nvidia_p2p_stub: addr_table init failed: %d\n", ret);
      return ret;
   }

   pr_info("nvidia_p2p_stub: /dev/nvidia_p2p_stub_mem miscdevice registered\n");
   return 0;
}

static void __exit nvidia_p2p_stub_exit(void)
{
   emu_gpu_addr_table_exit();
}

module_init(nvidia_p2p_stub_init);
module_exit(nvidia_p2p_stub_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SLAC National Accelerator Laboratory");
MODULE_DESCRIPTION("NVIDIA P2P API stub for datadev CI testing");
MODULE_VERSION("1.0");
