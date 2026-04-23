/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Stub-owned address-translation table. Owns the
 *    DEFINE_HASHTABLE(addr_ht, 6) map keyed by fake_dma, the alloc_pages
 *    backing buffer for each registered entry, and the drain-callback
 *    registration state. Exports emu_gpu_addr_lookup,
 *    emu_gpu_register_drain_cb, emu_gpu_unregister_drain_cb as
 *    EXPORT_SYMBOL_GPL for the datadev_emulator.ko consumer.
 *----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the aes_stream_drivers package, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 *----------------------------------------------------------------------------
 **/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/hashtable.h>
#include <linux/rcupdate.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include "emu_gpu_addr_table.h"
#include "emu_gpu_addr_table_priv.h"

/* Cross-kernel shims so the stub builds against the 5.14(RHEL9)/5.15/
 * 6.1/6.8/6.17 matrix CI exercises.
 *
 * MAX_PAGE_ORDER: introduced in Linux 6.8 (commit 5e0a760b4441). On older
 * kernels the equivalent value is (MAX_ORDER - 1) because pre-6.8 MAX_ORDER
 * was "one past the max" while MAX_PAGE_ORDER is the inclusive maximum.
 *
 * vm_flags_set: introduced in Linux 6.3 (commit 1c71222e5f237) and
 * backported to RHEL 9.4 (kernel 5.14.0-427). On older kernels vma->vm_flags
 * is a direct-assignment unsigned long; on 6.3+/RHEL-9.4+ vm_flags is
 * read-only and must be mutated via the helper. Match the convention in
 * common/driver/dma_common.c: guard on LINUX_VERSION_CODE || RHEL_RELEASE_CODE. */
#ifndef RHEL_RELEASE_VERSION
#define RHEL_RELEASE_VERSION(...) 0
#endif

#ifndef MAX_PAGE_ORDER
#define MAX_PAGE_ORDER (MAX_ORDER - 1)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0) && \
    (!defined(RHEL_RELEASE_CODE) || RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(9, 4))
static inline void vm_flags_set(struct vm_area_struct *vma, vm_flags_t flags)
{
   vma->vm_flags |= flags;
}
#endif

/* ----------------------------------------------------------------------- */
/* File-static state                                                         */
/* ----------------------------------------------------------------------- */

static DEFINE_HASHTABLE(addr_ht, EMU_GPU_ADDR_HASH_BITS);  /* 64 buckets   */
static DEFINE_SPINLOCK(addr_ht_lock);    /* mutate-side lock                 */
static atomic_t entry_idx_counter = ATOMIC_INIT(0);

static DEFINE_SPINLOCK(drain_cb_lock);   /* guards drain_cb + invocation    */
static void (*drain_cb)(void);           /* NULL if no consumer registered  */

/* ----------------------------------------------------------------------- */
/* Internal helper: stub_try_drain                                           */
/* ----------------------------------------------------------------------- */

/* Invoked from emu_gpu_addr_table_release after RCU quiescence + entry
 * free. If the hashtable is now empty AND a drain callback is registered,
 * call it once. Runs in process context (nvidia_p2p_free_page_table /
 * ioctl release path); safe to take drain_cb_lock as a plain spinlock. */
static void stub_try_drain(void)
{
   void (*cb)(void) = NULL;
   unsigned long flags;

   /* Hold addr_ht_lock while checking emptiness so this snapshot is
    * coherent with concurrent emu_gpu_addr_table_alloc_and_register
    * calls that add entries under the same lock (TOCTOU fix).
    * Lock ordering: addr_ht_lock (outer) -> drain_cb_lock (inner). */
   spin_lock_irqsave(&addr_ht_lock, flags);
   if (hash_empty(addr_ht)) {
      spin_lock(&drain_cb_lock);
      cb = drain_cb;
      spin_unlock(&drain_cb_lock);
   }
   spin_unlock_irqrestore(&addr_ht_lock, flags);

   if (cb)
      cb();
}

/* ----------------------------------------------------------------------- */
/* EXPORT_SYMBOL_GPL: emu_gpu_addr_lookup                                    */
/* ----------------------------------------------------------------------- */

/* Lookup on the hot path (emulator poll kthread, ~1 ms cadence). RCU
 * reader — NEVER sleeps, NEVER takes a spinlock. Hash key is
 * `fake_dma >> 16` so all dma_addresses[i] within one buffer (i=0..N at
 * 64KB stride) hash to the same bucket as the base, and the exact-match
 * `entry->fake_dma == fake_dma` filter accepts only the base address.
 * Emulator always queries with dma_addresses[0] (the base) per
 * gpu_async.c:230. */
void *emu_gpu_addr_lookup(u64 fake_dma)
{
   struct emu_gpu_addr_entry *entry;
   void *kva = NULL;

   rcu_read_lock();
   hash_for_each_possible_rcu(addr_ht, entry, node, fake_dma >> 16) {
      if (entry->fake_dma == fake_dma) {
         kva = entry->kva;
         break;
      }
   }
   rcu_read_unlock();
   return kva;
}
EXPORT_SYMBOL_GPL(emu_gpu_addr_lookup);

/* ----------------------------------------------------------------------- */
/* EXPORT_SYMBOL_GPL: drain callback registration                            */
/* ----------------------------------------------------------------------- */

int emu_gpu_register_drain_cb(void (*cb)(void))
{
   int ret = 0;

   if (!cb)
      return -EINVAL;

   spin_lock(&drain_cb_lock);
   if (drain_cb)
      ret = -EBUSY;
   else
      drain_cb = cb;
   spin_unlock(&drain_cb_lock);
   return ret;
}
EXPORT_SYMBOL_GPL(emu_gpu_register_drain_cb);

void emu_gpu_unregister_drain_cb(void)
{
   spin_lock(&drain_cb_lock);
   drain_cb = NULL;
   spin_unlock(&drain_cb_lock);
}
EXPORT_SYMBOL_GPL(emu_gpu_unregister_drain_cb);

/* ----------------------------------------------------------------------- */
/* Internal API: emu_gpu_addr_table_alloc_and_register                      */
/* ----------------------------------------------------------------------- */

int emu_gpu_addr_table_alloc_and_register(u64 length,
                                          struct emu_gpu_addr_entry **out)
{
   struct emu_gpu_addr_entry *entry;
   unsigned int order;
   unsigned long flags;
   u32 idx;

   if (!out || length == 0)
      return -EINVAL;

   order = get_order(length);
   /* Defence-in-depth clamp: MAX_PAGE_ORDER on this kernel is 10 (4MB)
    * for x86_64 without CONFIG_ARCH_FORCE_MAX_ORDER set. The largest
    * allocation never exceeds order-8 (1MB). A userspace-controlled size
    * reaching this path through the ioctl must NOT be able to request
    * order > 10. */
   if (order > MAX_PAGE_ORDER) {
      pr_err("nvidia_p2p_stub: alloc_and_register: order %u exceeds MAX_PAGE_ORDER\n",
             order);
      return -EINVAL;
   }

   entry = kzalloc(sizeof(*entry), GFP_KERNEL);
   if (!entry)
      return -ENOMEM;

   /* Single-shot alloc_pages, no retry/fallback loop. */
   entry->backing_pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
   if (!entry->backing_pages) {
      kfree(entry);
      return -ENOMEM;
   }

   idx = (u32)atomic_fetch_add(1, &entry_idx_counter);
   entry->fake_dma = STUB_FAKE_DMA_BASE +
                     ((u64)idx << STUB_FAKE_DMA_IDX_SHIFT);
   entry->kva   = page_address(entry->backing_pages);
   entry->order = order;
   entry->size  = (size_t)PAGE_SIZE << order;
   entry->idx   = idx;
   refcount_set(&entry->refcount, 1);
   INIT_HLIST_NODE(&entry->node);

   spin_lock_irqsave(&addr_ht_lock, flags);
   hash_add_rcu(addr_ht, &entry->node, entry->fake_dma >> 16);
   spin_unlock_irqrestore(&addr_ht_lock, flags);

   *out = entry;
   return 0;
}

/* ----------------------------------------------------------------------- */
/* Internal API: emu_gpu_addr_table_get                                      */
/* ----------------------------------------------------------------------- */

void emu_gpu_addr_table_get(struct emu_gpu_addr_entry *entry)
{
   if (entry)
      refcount_inc(&entry->refcount);
}

/* ----------------------------------------------------------------------- */
/* Internal API: emu_gpu_addr_table_release                                  */
/* ----------------------------------------------------------------------- */

void emu_gpu_addr_table_release(struct emu_gpu_addr_entry *entry)
{
   unsigned long flags;

   if (!entry)
      return;

   if (!refcount_dec_and_test(&entry->refcount)) {
      /* Other holder still has a reference; just drop ours. The entry
       * stays in the hashtable. */
      return;
   }

   /* We took the last reference. Unlink from the hashtable under the
    * mutate-side spinlock, then wait for RCU quiescence so any in-flight
    * emu_gpu_addr_lookup on the poll thread finishes, then free the
    * backing pages + entry struct, then attempt drain. */
   spin_lock_irqsave(&addr_ht_lock, flags);
   hash_del_rcu(&entry->node);
   spin_unlock_irqrestore(&addr_ht_lock, flags);

   synchronize_rcu();

   __free_pages(entry->backing_pages, entry->order);
   kfree(entry);

   /* Hashtable may have transitioned to empty — invoke drain cb if set. */
   stub_try_drain();
}

/* ----------------------------------------------------------------------- */
/* Internal API: emu_gpu_addr_table_find_by_idx                              */
/* ----------------------------------------------------------------------- */

struct emu_gpu_addr_entry *emu_gpu_addr_table_find_by_idx(u32 idx)
{
   struct emu_gpu_addr_entry *entry, *found = NULL;
   unsigned long flags;
   int bucket;

   spin_lock_irqsave(&addr_ht_lock, flags);
   hash_for_each(addr_ht, bucket, entry, node) {
      if (entry->idx == idx) {
         found = entry;
         /* Bump refcount before dropping the lock so the caller holds
          * a reference that prevents concurrent stub_release from
          * freeing the entry (use-after-free prevention in stub_mmap). */
         emu_gpu_addr_table_get(found);
         break;
      }
   }
   spin_unlock_irqrestore(&addr_ht_lock, flags);
   return found;
}

/* ----------------------------------------------------------------------- */
/* Internal API: emu_gpu_addr_is_stub_vma                                    */
/* ----------------------------------------------------------------------- */

/* Forward declaration of the file-static stub_fops (defined below at the
 * miscdevice fops table). Needed so the predicate above the fops table
 * can compare `vma->vm_file->f_op` against its address without un-
 * static-ing the table. */
static const struct file_operations stub_fops;

/* Predicate: true iff `vma` is a /dev/nvidia_p2p_stub_mem mapping
 * produced by stub_mmap. Used by nvidia_p2p_stub.c to decide whether
 * nvidia_p2p_get_pages should reuse an existing entry (VMA-reuse path)
 * or fall through to emu_gpu_addr_table_alloc_and_register.
 *
 * No lock needed: stub_fops is a module-lifetime static const, and the
 * caller holds mmap_read_lock(current->mm) which pins `vma->vm_file`
 * against concurrent munmap. */
bool emu_gpu_addr_is_stub_vma(const struct vm_area_struct *vma)
{
   return vma && vma->vm_file && vma->vm_file->f_op == &stub_fops;
}

/* ----------------------------------------------------------------------- */
/* Per-FD state types for miscdevice                                         */
/* ----------------------------------------------------------------------- */

struct stub_fd_hold {
   struct emu_gpu_addr_entry *entry;
   struct list_head           node;
};

struct stub_fd_state {
   struct list_head  held_entries;   /* list of stub_fd_hold */
   struct mutex      lock;           /* guards held_entries  */
};

/* ----------------------------------------------------------------------- */
/* Miscdevice fops: stub_open                                                */
/* ----------------------------------------------------------------------- */

static int stub_open(struct inode *inode, struct file *file)
{
   struct stub_fd_state *fs;

   (void)inode;
   fs = kzalloc(sizeof(*fs), GFP_KERNEL);
   if (!fs)
      return -ENOMEM;
   INIT_LIST_HEAD(&fs->held_entries);
   mutex_init(&fs->lock);
   file->private_data = fs;
   return 0;
}

/* ----------------------------------------------------------------------- */
/* Miscdevice fops: stub_release                                             */
/* ----------------------------------------------------------------------- */

static int stub_release(struct inode *inode, struct file *file)
{
   struct stub_fd_state *fs = file->private_data;
   struct stub_fd_hold *hold, *tmp;
   LIST_HEAD(to_free);

   (void)inode;
   if (!fs)
      return 0;

   /* Splice the entire held-entries list out under the lock, then
    * release entries outside the lock.  emu_gpu_addr_table_release may
    * call synchronize_rcu (blocking for a full RCU grace period) when
    * the refcount hits zero; holding fs->lock across that would block
    * any concurrent stub_ioctl / stub_mmap on the same FD. */
   mutex_lock(&fs->lock);
   list_splice_init(&fs->held_entries, &to_free);
   mutex_unlock(&fs->lock);

   list_for_each_entry_safe(hold, tmp, &to_free, node) {
      list_del(&hold->node);
      /* Drop the sole refcount for this FD-owned entry (single-holder
       * lineage: alloc_and_register sets refcount=1; FD closure drops
       * it to 0 -> __free_pages + drain-cb). */
      emu_gpu_addr_table_release(hold->entry);
      kfree(hold);
   }

   mutex_destroy(&fs->lock);
   kfree(fs);
   file->private_data = NULL;
   return 0;
}

/* ----------------------------------------------------------------------- */
/* Miscdevice fops: stub_ioctl (STUB_RESERVE_BUF only)                      */
/* ----------------------------------------------------------------------- */

static long stub_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
   struct stub_fd_state *fs = file->private_data;
   struct stub_reserve_req req;
   struct emu_gpu_addr_entry *entry;
   struct stub_fd_hold *hold;
   int ret;

   if (cmd != STUB_RESERVE_BUF)
      return -ENOTTY;

   if (!fs)
      return -EBADF;

   if (copy_from_user(&req, (void __user *)arg, sizeof(req))) {
      pr_err("nvidia_p2p_stub: copy_from_user failed\n");
      return -EFAULT;
   }

   /* Reject zero and anything larger than the max order alloc_pages can
    * serve on this kernel (MAX_PAGE_ORDER=10 => 4MB on x86_64 at
    * PAGE_SIZE=4KB). The largest payload is 1MB (order 8), so 4MB
    * leaves headroom without exposing userspace to order > MAX_PAGE_ORDER. */
   if (req.size == 0 ||
       req.size > (1UL << (MAX_PAGE_ORDER + PAGE_SHIFT))) {
      return -EINVAL;
   }

   ret = emu_gpu_addr_table_alloc_and_register((u64)req.size, &entry);
   if (ret)
      return ret;

   /* Single-holder-per-lineage: ioctl-created entries have refcount=1
    * (from alloc_and_register). The FD holds THE lone reference. No
    * separate _get call; stub_release does one release per held entry. */
   hold = kzalloc(sizeof(*hold), GFP_KERNEL);
   if (!hold) {
      emu_gpu_addr_table_release(entry);  /* drops sole refcount -> free */
      return -ENOMEM;
   }
   hold->entry = entry;

   mutex_lock(&fs->lock);
   list_add(&hold->node, &fs->held_entries);
   mutex_unlock(&fs->lock);

   req.buf_id = entry->idx;
   if (copy_to_user((void __user *)arg, &req, sizeof(req))) {
      /* Partial success: remove the hold, drop the sole ref, return EFAULT. */
      mutex_lock(&fs->lock);
      list_del(&hold->node);
      mutex_unlock(&fs->lock);
      kfree(hold);
      emu_gpu_addr_table_release(entry);  /* drops sole refcount -> free */
      return -EFAULT;
   }

   return 0;
}

/* ----------------------------------------------------------------------- */
/* Miscdevice fops: stub_mmap                                                */
/* ----------------------------------------------------------------------- */

static int stub_mmap(struct file *file, struct vm_area_struct *vma)
{
   u32 buf_id = (u32)vma->vm_pgoff;   /* kernel pre-divided by PAGE_SIZE */
   unsigned long size = vma->vm_end - vma->vm_start;
   struct emu_gpu_addr_entry *entry;

   (void)file;
   entry = emu_gpu_addr_table_find_by_idx(buf_id);
   if (!entry) {
      pr_err("nvidia_p2p_stub: mmap buf_id=%u not found\n", buf_id);
      return -EINVAL;
   }
   if (size > entry->size) {
      pr_err("nvidia_p2p_stub: mmap size=%lu exceeds entry size=%zu\n",
             size, entry->size);
      emu_gpu_addr_table_release(entry);
      return -EINVAL;
   }

   /* Pitfall D: vm_flags_set BEFORE the range mapping call. */
   vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTDUMP);

   /* find_by_idx bumped the refcount; release it now that we are done
    * accessing entry->backing_pages.  remap_pfn_range pins the pages
    * via the VMA pte, so the underlying memory stays mapped. */
   remap_pfn_range(vma, vma->vm_start,
                   page_to_pfn(entry->backing_pages),
                   size, vma->vm_page_prot);
   emu_gpu_addr_table_release(entry);
   return 0;
}

/* ----------------------------------------------------------------------- */
/* struct file_operations + struct miscdevice                                */
/* ----------------------------------------------------------------------- */

static const struct file_operations stub_fops = {
   .owner          = THIS_MODULE,
   .open           = stub_open,
   .release        = stub_release,
   .unlocked_ioctl = stub_ioctl,
   .compat_ioctl   = stub_ioctl,
   .mmap           = stub_mmap,
};

static struct miscdevice stub_miscdev = {
   .minor = MISC_DYNAMIC_MINOR,
   .name  = "nvidia_p2p_stub_mem",
   .fops  = &stub_fops,
};

/* ----------------------------------------------------------------------- */
/* Module-lifecycle helpers                                                  */
/* ----------------------------------------------------------------------- */

int emu_gpu_addr_table_init(void)
{
   int ret;

   hash_init(addr_ht);

   ret = misc_register(&stub_miscdev);
   if (ret) {
      pr_err("nvidia_p2p_stub: misc_register failed: %d\n", ret);
      return ret;
   }
   return 0;
}

void emu_gpu_addr_table_exit(void)
{
   misc_deregister(&stub_miscdev);
   /* The driver-side free_page_table path and miscdevice FD release
    * should have drained the table. If anything remains, warn loudly:
    * a non-empty table at rmmod time means the drain-cb protocol did
    * not run, and the emulator's symbol_put was never called — rmmod
    * on nvidia_p2p_stub will have failed with -EWOULDBLOCK anyway. */
   WARN_ON(!hash_empty(addr_ht));
}
