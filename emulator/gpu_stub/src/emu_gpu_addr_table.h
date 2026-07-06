/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Cross-module contract header for the nvidia_p2p_stub address-handoff
 *    table. Declares the kernel-only struct emu_gpu_addr_entry, the three
 *    EXPORT_SYMBOL_GPL function prototypes, and the uapi ioctl surface
 *    (STUB_RESERVE_BUF + struct stub_reserve_req) consumed by the userspace
 *    test harness.
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
#ifndef __EMU_GPU_ADDR_TABLE_H__
#define __EMU_GPU_ADDR_TABLE_H__

/* Kernel TUs pull the full hashtable/refcount surface; userspace TUs
 * (rdmaTestEmu, stub_mmap_test.c) only need the uapi struct + ioctl
 * macro. */
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/hashtable.h>
#include <linux/refcount.h>
#include <linux/ioctl.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
#endif

/* Opaque base for fabricated fake_dma keys. Keep the 0xDEADBEEF prefix
 * so any leaked pr_info still looks like the pre-Phase-2 magic value. */
#define STUB_FAKE_DMA_BASE        0xDEADBEEF00000000ULL
/* 64KB -- matches GPU_BOUND_SIZE in common/driver/gpu_async.h. */
#define STUB_PAGE_SIZE            ((uint64_t)1 << 16)
/* 16MB spacing per buffer: fake_dma_base = STUB_FAKE_DMA_BASE + ((u64)idx << 24). */
#define STUB_FAKE_DMA_IDX_SHIFT   24

/* Sent to ioctl(/dev/nvidia_p2p_stub_mem, STUB_RESERVE_BUF, &req).
 * Kernel reads .size, fills .buf_id. */
struct stub_reserve_req {
    uint32_t size;      /* in:  requested buffer size in bytes              */
    uint32_t buf_id;    /* out: entry index; mmap offset = buf_id<<PAGE_SHIFT */
};
#define STUB_RESERVE_BUF  _IOWR('S', 1, struct stub_reserve_req)

#ifdef __KERNEL__

/* Hashtable buckets for the fake_dma -> kva map. 2^6 = 64 buckets. */
#define EMU_GPU_ADDR_HASH_BITS    6

/* Per-buffer address table entry. Allocated once per nvidia_p2p_get_pages
 * success and once per STUB_RESERVE_BUF ioctl success. Freed when refcount
 * hits zero via refcount_dec_and_test. */
struct emu_gpu_addr_entry {
    u64               fake_dma;        /* key: fake_dma_base for this buffer */
    void             *kva;             /* page_address(backing_pages)        */
    struct page      *backing_pages;
    unsigned int      order;           /* alloc_pages order                  */
    size_t            size;            /* bytes: (size_t)PAGE_SIZE << order  */
    u32               idx;             /* buf_id: miscdevice mmap offset     */
    refcount_t        refcount;        /* 1 driver-holder + 0..1 FD-holder   */
    struct hlist_node node;            /* embedded in addr_ht                */

    /* NVIDIA free_callback registered by the datadev driver via
     * nvidia_p2p_get_pages(). Real NVIDIA fires this when the GPU
     * allocation is freed/revoked; the stub's analog is the app-side
     * owner (the STUB_RESERVE_BUF FD) going away. Invoked once from
     * stub_release so the driver's Gpu_FreeNvidia -> nvidia_p2p_free_
     * page_table drops its holder reference and the table can drain.
     * NULL for app-only reservations the driver never mapped. */
    void            (*free_cb)(void *);
    void             *free_cb_data;
    bool              free_cb_fired;   /* idempotency guard (fire at most once) */
};

/* --- EXPORT_SYMBOL_GPL surface ---
 * All three symbols are GPL-only. Existing five nvidia_p2p_* symbols in
 * nvidia_p2p_stub.c stay on plain EXPORT_SYMBOL (ABI-compat). */

/* Looks up the kernel virtual address for a fake_dma opaque key. Returns
 * NULL if the key is not registered. Safe to call from RCU read-side
 * (rcu_read_lock held). NEVER sleeps. */
void *emu_gpu_addr_lookup(u64 fake_dma);

/* Register the drain callback invoked when the address table transitions
 * to empty. Called once by emulator during emu_gpu_init. The callback runs in process context (after synchronize_rcu) and must call
 * symbol_put(emu_gpu_addr_lookup) to release the pin. Only one callback
 * may be registered at a time; returns -EBUSY if a callback is already
 * registered. */
int  emu_gpu_register_drain_cb(void (*cb)(void));

/* Unregister the drain callback. Called once by emulator during
 * emu_exit. Safe to call even if no callback was registered. */
void emu_gpu_unregister_drain_cb(void);

#endif /* __KERNEL__ */

#endif /* __EMU_GPU_ADDR_TABLE_H__ */
