/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    Main entry point for the datadev emulator kernel module.
 *    Integrates BAR0 register allocation, virtual IRQ creation, and
 *    PCI host bridge setup into module_init/module_exit lifecycle.
 *
 *    Loading this module creates a virtual PCI device that the real
 *    datadev driver can probe without physical FPGA hardware.
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/random.h>
#include "bar0_regs.h"
#include "virt_pci_host.h"
#include "virt_irq.h"
#include "dma_engine.h"
#include "gpu_engine.h"
/* Drain-callback registration surface from nvidia_p2p_stub. */
#include "../../gpu_stub/src/emu_gpu_addr_table.h"

#define EMU_MOD_NAME "datadev_emulator"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SLAC National Accelerator Laboratory");
MODULE_DESCRIPTION("Virtual PCI device emulator for datadev driver CI testing");
MODULE_VERSION("1.0");

/* Max GPU-async buffers. Bounded by V4 register field width [10:0] = 1024.
 * Out-of-range values cause insmod to fail with -EINVAL in emu_init()
 * before any BAR0 or engine allocation runs. */
static uint emu_gpu_max_buffers = 4;
module_param(emu_gpu_max_buffers, uint, 0444);
MODULE_PARM_DESC(emu_gpu_max_buffers,
                 "Max GPU buffers (1..1024, default 4)");

/* PRBS seed. 0 = pick a random u32 via get_random_u32() at init so each CI
 * run uses a different seed; any non-zero value is passed through verbatim
 * for reproducible replays. The resolved seed is logged via pr_info so the
 * same value can be supplied to a follow-up insmod. */
static uint emu_prbs_seed;
module_param(emu_prbs_seed, uint, 0444);
MODULE_PARM_DESC(emu_prbs_seed,
                 "PRBS seed (0 = random via get_random_u32)");

/* Gates a pr_info in emu_gpu_rx_tick that emits "emu_gpu: buf=%u
 * rx_seq=0x%08x" per RX-completed frame. Default 0 (off) so soak dmesg
 * is clean. Set to 1 during bring-up to observe per-buffer PRBS
 * monotonicity.
 *
 * Non-static so gpu_engine.c (same datadev_emulator.ko) can reference
 * it via `extern uint emu_gpu_debug_sc2;` — matches the cross-TU
 * scoping used for emu_gpu_drop_pin et al. */
uint emu_gpu_debug_sc2;
module_param(emu_gpu_debug_sc2, uint, 0444);
MODULE_PARM_DESC(emu_gpu_debug_sc2,
                 "Per-frame RX instrumentation (0=off default, 1=on)");

/* Read-only sysfs readout of EMU_BUILD_VERSION macro so
 * tests/test_gpu_dma_loopback.sh can assert the running .ko matches the
 * freshly-built .build_version sideband file (stale-module gate). */
static unsigned int emu_build_version = EMU_BUILD_VERSION;
module_param_named(build_version, emu_build_version, uint, 0444);
MODULE_PARM_DESC(build_version,
                 "Read-only EMU_BUILD_VERSION stamp (seconds since epoch at build time)");

/* emu_gpu_poll kthread sleep interval in microseconds. Default 1000 matches
 * the pre-existing 900-1100 usleep_range. Nested-KVM CI runners oversleep usleep_range dramatically
 * under scheduler contention, starving the poll thread long enough for
 * userspace per-doorbell deadlines to trip; pass a smaller value (e.g. 200)
 * via load-modules-gpu.sh to keep the tick responsive under pressure.
 *
 * Writable via sysfs (0644) so a stalling dev session can reduce the
 * interval without unloading the module. Non-static so gpu_engine.c can
 * reference it via `extern uint emu_gpu_poll_interval_us;`. */
uint emu_gpu_poll_interval_us = 1000;
module_param(emu_gpu_poll_interval_us, uint, 0644);
MODULE_PARM_DESC(emu_gpu_poll_interval_us,
                 "emu_gpu_poll kthread usleep_range min (µs, default 1000, floor 10)");

/* File-static global state */
static struct emu_bar0 emu_bar;
static struct emu_irq emu_irq;
static struct emu_dma_engine emu_dma;
static struct emu_pci_host emu_host;
static struct emu_gpu_engine emu_gpu;

/* ----------------------------------------------------------------
 * Drain callback.
 *
 * Invoked by nvidia_p2p_stub when its fake_dma -> kva hashtable
 * transitions to empty. Runs in process context (after synchronize_rcu
 * in the stub), so mutex_lock inside emu_gpu_drop_pin is safe. Single
 * job: drop the symbol_get pin the poll thread took on first real
 * work.
 * ---------------------------------------------------------------- */
static void emu_on_stub_empty(void)
{
   emu_gpu_drop_pin();
}

/**
 * emu_init - Module initialization
 *
 * Performs the complete init sequence:
 *   1. Allocate BAR0 memory region
 *   2. Claim BAR0 phys range in iomem_resource
 *   3. Populate BAR0 with initial register values
 *   4. Create IRQ domain and virtual IRQ
 *   5. Initialize DMA loopback engine
 *   6. Start DMA engine polling (BEFORE driver probe, so init WriteFree
 *      calls are captured and the free-buffer pool is populated)
 *   7. Create PCI host bridge and scan bus (triggers driver probe)
 *   8. Register PCI bus notifier for CMA rebind protection
 *
 * Uses goto-based cleanup on error to free resources already allocated.
 */
static int __init emu_init(void)
{
   int ret;

   pr_info("%s: loading emulator module\n", EMU_MOD_NAME);

   /* Reject out-of-range emu_gpu_max_buffers BEFORE any allocation so
    * we never leak resources on a misconfigured insmod. The V4 BAR0
    * MaxBuffersV4 register field is [10:0] -> upper bound 1024. Zero
    * is also illegal because gpu_engine's rx/tx ticks modulo by this
    * count -- a zero value would divide-by-zero. */
   if (emu_gpu_max_buffers == 0 || emu_gpu_max_buffers > 1024) {
      pr_err("%s: invalid emu_gpu_max_buffers=%u (must be 1..1024)\n",
             EMU_MOD_NAME, emu_gpu_max_buffers);
      return -EINVAL;
   }

   /* Resolve the PRBS seed once so the same value appears in dmesg and
    * in every subsequent engine init. emu_prbs_seed=0 means "pick a
    * random value now"; any non-zero value is passed through verbatim
    * for reproducible runs. */
   if (emu_prbs_seed == 0)
      emu_prbs_seed = get_random_u32();
   pr_info("%s: prbs seed=0x%08x\n", EMU_MOD_NAME, emu_prbs_seed);

   /* Step 1: Allocate BAR0 memory */
   ret = emu_bar0_alloc(&emu_bar);
   if (ret) {
      pr_err("%s: BAR0 allocation failed (%d)\n", EMU_MOD_NAME, ret);
      return ret;
   }

   /* Step 2: Claim the BAR0 phys range in iomem_resource so consumer
    * driver's request_mem_region succeeds.  Must happen after alloc_pages
    * (phys is known) and before pci_bus_add_devices triggers driver probe. */
   ret = emu_bar0_claim_iomem(&emu_bar);
   if (ret) {
      pr_err("%s: BAR0 iomem claim failed (%d)\n", EMU_MOD_NAME, ret);
      goto err_free_bar;
   }

   /* Step 3: Initialize register values. Propagate emu_gpu_max_buffers
    * into the BAR0 MaxBuffersV4 slot so the datadev driver's
    * Gpu_AddNvidia capacity check and the GPU_Get_Max_Buffers ioctl
    * both track the insmod parameter. */
   emu_bar0_init_regs(&emu_bar, emu_gpu_max_buffers);

   /* Step 4: Create IRQ domain and virtual IRQ */
   ret = emu_irq_create(&emu_irq);
   if (ret) {
      pr_err("%s: IRQ creation failed (%d)\n", EMU_MOD_NAME, ret);
      goto err_release_iomem;
   }

   /* Step 5: Initialize DMA engine */
   ret = emu_dma_init(&emu_dma, &emu_bar, &emu_irq);
   if (ret) {
      pr_err("%s: DMA engine init failed (%d)\n", EMU_MOD_NAME, ret);
      goto err_destroy_irq;
   }

   /* Step 6: Start DMA engine polling BEFORE creating the PCI host bridge.
    * The poll thread must be running when pci_bus_add_devices() triggers
    * the datadev driver's probe so it can capture the ~1024 WriteFree calls
    * that AxisG2_Init issues immediately after writing the ring base
    * addresses.  If the thread starts after probe, it only sees the last
    * WriteFree value (single register, overwritten for each buffer) and the
    * free-buffer pool stays at 0 or 1 entry, causing every TX loopback to
    * complete with size=0 and RxCount to remain zero in all tests. */
   emu_dma_start(&emu_dma);

   /* Step 6b: Initialize and start the GPU-async half-FSM engine.
    * Must start BEFORE pci_host_create for the same reason dma_start
    * does: the datadev driver's probe immediately writes writeEnable=1
    * on V4 devices (gpu_async.c Gpu_SetWriteEn), and the gpu poll
    * thread must already be ticking when that happens or the first
    * round of doorbells silently drops.
    *
    * Also propagate the resolved PRBS seed into both rx/tx sequence
    * counters so the generator (RX) and verifier (TX) share the same
    * starting point across every CI run. */
   ret = emu_gpu_init(&emu_gpu, &emu_bar);
   if (ret) {
      pr_err("%s: GPU engine init failed (%d)\n", EMU_MOD_NAME, ret);
      goto err_stop_dma;
   }
   emu_gpu.rx_prbs_seq = emu_prbs_seed;

   ret = emu_gpu_start(&emu_gpu);
   if (ret) {
      pr_err("%s: GPU engine start failed (%d)\n", EMU_MOD_NAME, ret);
      goto err_destroy_gpu;
   }

   /* Step 6c: Register drain callback with nvidia_p2p_stub.
    * Eager resolution — requires nvidia_p2p_stub.ko to be loaded before
    * datadev_emulator.ko or insmod fails with "Unknown symbol
    * emu_gpu_register_drain_cb". CI load order documented in
    * scripts/ci/load-modules-gpu.sh. */
   ret = emu_gpu_register_drain_cb(emu_on_stub_empty);
   if (ret) {
      pr_err("%s: emu_gpu_register_drain_cb failed (%d)\n",
             EMU_MOD_NAME, ret);
      goto err_stop_gpu;
   }

   /* Step 7: Create PCI host bridge -- triggers bus scan and driver probe.
    * emu_pci_host_create() sets pcidev->irq = virq before
    * pci_bus_add_devices() so the datadev driver's IRQ check at probe
    * time succeeds. */
   ret = emu_pci_host_create(&emu_host, &emu_bar, emu_irq.virq);
   if (ret) {
      pr_err("%s: PCI host creation failed (%d)\n", EMU_MOD_NAME, ret);
      goto err_unregister_drain;
   }

   /* Step 8: Register PCI bus notifier to re-clear cma_area on every
    * datadev rebind. Prevents stale-pointer GPF in __cma_alloc when
    * BUFF_STREAM -> BUFF_COHERENT reload cycles occur. */
   ret = emu_pci_notifier_init(&emu_host);
   if (ret) {
      pr_err("%s: PCI bus notifier registration failed (%d)\n", EMU_MOD_NAME, ret);
      goto err_destroy_host;
   }

   pr_info("%s: emulator loaded successfully\n", EMU_MOD_NAME);
   pr_info("%s: build_version=%u BAR0 phys=0x%llx size=0x%lx virq=%u\n",
           EMU_MOD_NAME,
           EMU_BUILD_VERSION,
           (unsigned long long)emu_bar.phys,
           emu_bar.size,
           emu_irq.virq);

   return 0;

err_destroy_host:
   emu_pci_host_destroy(&emu_host);
err_unregister_drain:
   emu_gpu_unregister_drain_cb();
err_stop_gpu:
   emu_gpu_stop(&emu_gpu);
err_destroy_gpu:
   emu_gpu_destroy(&emu_gpu);
err_stop_dma:
   emu_dma_stop(&emu_dma);
   /* dma_destroy follows unconditionally via fall-through (no goto
    * target lands here because emu_dma_init failure skips the
    * corresponding stop step). */
   emu_dma_destroy(&emu_dma);
err_destroy_irq:
   emu_irq_destroy(&emu_irq);
err_release_iomem:
   emu_bar0_release_iomem(&emu_bar);
err_free_bar:
   emu_bar0_free(&emu_bar);
   return ret;
}

/**
 * emu_exit - Module cleanup
 *
 * Reverse order of init:
 *   1. Stop DMA polling (cancel workqueue before driver remove)
 *   2. Destroy PCI host bridge (triggers driver remove callbacks)
 *   3. Destroy DMA engine (flush workqueue after PCI host gone)
 *   4. Destroy IRQ domain
 *   5. Release BAR0 iomem_resource window and restore System RAM
 *   6. Free BAR0 memory
 */
static void __exit emu_exit(void)
{
   pr_info("%s: unloading emulator module\n", EMU_MOD_NAME);

   /* Step 1: Unregister PCI bus notifier BEFORE destroying the host bridge,
    * because destroy triggers device-removal events whose filter would
    * otherwise no longer match cleanly. */
   emu_pci_notifier_destroy(&emu_host);

   /* Step 2: Stop DMA polling -- prevents poll workqueue from racing
    * with driver teardown triggered by PCI host destroy */
   emu_dma_stop(&emu_dma);

   /* Step 2b: Stop GPU-async poll thread BEFORE destroying the PCI host
    * so the datadev driver's Gpu_SetWriteEn(0) during remove does not
    * race with a still-running poll tick (TX/RX ack doorbell ordering
    * symmetry with the DMA stop above). */
   emu_gpu_stop(&emu_gpu);

   /* Unregister drain callback BEFORE destroying the PCI host (which
    * triggers driver remove and the last free_page_table calls).
    * Unregister clears the stub's drain_cb slot so the stub won't call
    * a callback in code that's about to unload. */
   emu_gpu_unregister_drain_cb();

   /* Defensive: drop the pin ourselves in case the drain callback
    * never fired (e.g. no Gpu_AddNvidia ever ran, so the hashtable
    * never had entries to drain). Idempotent — no-op if have_pin
    * is already false. */
   emu_gpu_drop_pin();

   /* Step 2: Destroy PCI host bridge -- triggers driver remove callbacks.
    * The datadev driver's remove path calls release_mem_region on the
    * BAR0 window, so our iomem carve must still be in place here. */
   emu_pci_host_destroy(&emu_host);

   /* Step 3b: Destroy GPU engine state after the driver is gone -- the
    * poll thread is already stopped above; destroy is now just freeing
    * any residual engine state (idempotent if stop already ran). */
   emu_gpu_destroy(&emu_gpu);

   /* Step 3: Destroy DMA engine -- flush and destroy workqueue after
    * PCI host is gone but before IRQ domain teardown, since
    * irq_work may still be pending until the workqueue is flushed */
   emu_dma_destroy(&emu_dma);

   /* Step 4: Destroy IRQ domain */
   emu_irq_destroy(&emu_irq);

   /* Step 5: Release BAR0 iomem window and restore the original System
    * RAM extents in iomem_resource.  Safe to run after driver remove
    * because consumer drivers have already called release_mem_region. */
   emu_bar0_release_iomem(&emu_bar);

   /* Step 6: Free BAR0 memory */
   emu_bar0_free(&emu_bar);

   pr_info("%s: emulator unloaded\n", EMU_MOD_NAME);
}

module_init(emu_init);
module_exit(emu_exit);
