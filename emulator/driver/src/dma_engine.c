/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    DMA loopback engine implementation for the PCI device emulator.
 *
 *    Hard-wired to 128-bit descriptor mode (Desc128En=1). 64-bit
 *    descriptor mode is not emulated; see bar0_regs.h for the enableVer
 *    field layout and the R/O enforcement contract the poll thread
 *    implements.
 *
 *    WriteFree capture: the driver writes ~1024 WriteFree calls during
 *    init.  Each writes writeFifoB then writeFifoA (single shadow
 *    registers).  A kthread busy-polls with cpu_relax() to capture as
 *    many as possible; any that are missed produce a reduced free pool
 *    but the loopback still works with the captured subset.
 *
 *    WriteTx capture: in steady-state the driver sends TX descriptors
 *    one at a time from user-space, giving the 1ms poll ample time
 *    to capture each one.  readFifoA (last written) is the detection
 *    register.
 *
 *    Ring writeback uses 128-bit (4-word) entries per AxisG2_MapReturn.
 *    ptr[3] is the validity word (chan<<8 | dest); must be non-zero.
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
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <asm/barrier.h>

#include "dma_engine.h"
#include "bar0_regs.h"

/* ----------------------------------------------------------------
 * Register access helpers
 *
 * Both the emulator and the datadev driver access BAR0 through UC
 * (uncacheable) ioremap mappings of the same physical pages, so
 * cache coherency is guaranteed — no flush needed.
 * ---------------------------------------------------------------- */

static inline uint32_t emu_reg_read(struct emu_dma_engine *eng,
                                    uint32_t offset)
{
   return ioread32(eng->reg_base + offset);
}

static inline void emu_reg_write(struct emu_dma_engine *eng,
                                 uint32_t offset, uint32_t val)
{
   iowrite32(val, eng->reg_base + offset);
}

/* ----------------------------------------------------------------
 * enableVer R/O field enforcement
 *
 * In the AxiStreamDmaV2Desc VHDL, only bit 0 of enableVer is R/W; the
 * enableCnt (bits 15:8), Desc128En (bit 16), and version (bits 31:24)
 * fields are axiSlaveRegisterR -- writes to them are silently discarded
 * by the AXI-lite decoder. The emulator backs BAR0 with ordinary RAM,
 * so the driver's writel(0x1)/writel(0x0) to enableVer overwrites the
 * whole word (the R/O fields included).  The consequence is a reload
 * hazard: AxisG2_Clear writes 0, the reserved bits stay 0 through
 * module unload, and the next insmod reads Desc128En=0 / version=0,
 * which silently disables 128-bit completion processing.
 *
 * The poll thread calls emu_enforce_enablever_ro on every cycle to
 * re-assert the R/O bits while preserving whatever bit 0 the driver
 * just wrote.  A 0->1 edge on bit 0 (AxisG2_Enable) advances
 * enable_cnt, matching the hardware's enableCnt counter.
 * ---------------------------------------------------------------- */

static void emu_enforce_enablever_ro(struct emu_dma_engine *eng)
{
   uint32_t cur = emu_reg_read(eng, EMU_REG_ENABLEVER);
   uint8_t enable_bit = cur & 0x1;
   uint32_t target;

   /* 0->1 edge on the enable bit is a new driver activation; bump the
    * enableCnt field (8-bit, wraps at 256 to match the VHDL width). */
   if (enable_bit && !eng->prev_enable)
      eng->enable_cnt++;
   eng->prev_enable = enable_bit;

   target = ((uint32_t)EMU_REG_ENABLEVER_VERSION << 24) |
            EMU_REG_ENABLEVER_DESC128 |
            ((uint32_t)eng->enable_cnt << EMU_REG_ENABLEVER_CNT_SHIFT) |
            (cur & EMU_REG_ENABLEVER_ENABLE_MASK);

   if (cur != target)
      emu_reg_write(eng, EMU_REG_ENABLEVER, target);
}

/* ----------------------------------------------------------------
 * Free buffer pool management
 * ---------------------------------------------------------------- */

static void emu_free_pool_push(struct emu_dma_engine *eng,
                               uint32_t index, dma_addr_t handle)
{
   unsigned long flags;

   spin_lock_irqsave(&eng->free_lock, flags);

   if (eng->free_count >= EMU_FREE_POOL_MAX) {
      spin_unlock_irqrestore(&eng->free_lock, flags);
      pr_warn("emu: free pool full, dropping buffer index=%u\n", index);
      return;
   }

   eng->free_pool[eng->free_count].index = index;
   eng->free_pool[eng->free_count].handle = handle;
   eng->free_count++;

   spin_unlock_irqrestore(&eng->free_lock, flags);
}

static bool emu_free_pool_pop(struct emu_dma_engine *eng,
                              uint32_t *out_index, dma_addr_t *out_handle)
{
   unsigned long flags;

   spin_lock_irqsave(&eng->free_lock, flags);

   if (eng->free_count == 0) {
      spin_unlock_irqrestore(&eng->free_lock, flags);
      return false;
   }

   eng->free_count--;
   *out_index = eng->free_pool[eng->free_count].index;
   *out_handle = eng->free_pool[eng->free_count].handle;

   spin_unlock_irqrestore(&eng->free_lock, flags);
   return true;
}

/* ----------------------------------------------------------------
 * Ring buffer capture
 * ---------------------------------------------------------------- */

static void emu_capture_rings(struct emu_dma_engine *eng)
{
   uint64_t wr_dma, rd_dma;
   uint32_t addr_width;

   wr_dma  = (uint64_t)emu_reg_read(eng, EMU_REG_WRBASEHIGH) << 32;
   wr_dma |= (uint64_t)emu_reg_read(eng, EMU_REG_WRBASELOW);

   rd_dma  = (uint64_t)emu_reg_read(eng, EMU_REG_RDBASEHIGH) << 32;
   rd_dma |= (uint64_t)emu_reg_read(eng, EMU_REG_RDBASELOW);

   addr_width = emu_reg_read(eng, EMU_REG_ADDRWIDTH);
   if (addr_width > 0 && addr_width <= 16)
      eng->addr_count = 1 << addr_width;
   else
      eng->addr_count = 4096;

   if (wr_dma != 0)
      eng->wr_ring = (uint32_t *)phys_to_virt((phys_addr_t)wr_dma);

   if (rd_dma != 0)
      eng->rd_ring = (uint32_t *)phys_to_virt((phys_addr_t)rd_dma);

   eng->rings_valid = (wr_dma != 0) && (rd_dma != 0);

   if (eng->rings_valid) {
      emu_reg_write(eng, EMU_REG_HWWRINDEX, 0);
      emu_reg_write(eng, EMU_REG_HWRDINDEX, 0);

      pr_info("emu: DMA rings captured: wr=%p rd=%p count=%u\n",
              eng->wr_ring, eng->rd_ring, eng->addr_count);
   }
}

/* ----------------------------------------------------------------
 * 128-bit DMA address reconstruction
 *
 * WriteFree 128-bit: wrData[0] = index | (handle<<24)&0xF0000000
 *                    wrData[1] = (handle>>8) & 0xFFFFFFFF
 *   => handle = (wrData[1] << 8) | ((wrData[0] >> 24) & 0xF0) >> 4
 *            = (wrData[1] << 8) | ((wrData[0] & 0xF0000000) >> 24)
 *
 * WriteTx  128-bit: rdData[2] = index | (handle<<24)&0xF0000000
 *                   rdData[3] = (handle>>8) & 0xFFFFFFFF
 *   => same reconstruction from fifo_c / fifo_d
 * ---------------------------------------------------------------- */

static dma_addr_t emu_reconstruct_handle(uint32_t lo, uint32_t hi)
{
   dma_addr_t handle;
   handle  = ((dma_addr_t)hi) << 8;
   handle |= ((dma_addr_t)(lo & 0xF0000000)) >> 24;
   return handle;
}

/* ----------------------------------------------------------------
 * FIFO capture: detect on the LAST-written register
 *
 * 128-bit WriteFree: driver writes writeFifoB then writeFifoA
 *   => detect on writeFifoA (non-zero: contains buffer index)
 *
 * 128-bit WriteTx: driver writes readFifoD, readFifoC, readFifoB, readFifoA
 *   => detect on readFifoA CAN be zero (flags=0,chan=0,dest=0,cont=0).
 *      Use readFifoB (size, always >0) as detection instead.
 * ---------------------------------------------------------------- */

static bool emu_capture_free_one(struct emu_dma_engine *eng)
{
   uint32_t fifo_a, fifo_b;
   uint32_t index;
   dma_addr_t handle;

   fifo_a = emu_reg_read(eng, EMU_REG_WRITEFIFOA);
   if (fifo_a == 0)
      return false;

   fifo_b = emu_reg_read(eng, EMU_REG_WRITEFIFOB);

   emu_reg_write(eng, EMU_REG_WRITEFIFOA, 0);

   index = fifo_a & 0x0FFFFFFF;
   handle = emu_reconstruct_handle(fifo_a, fifo_b);

   if (eng->free_captured < 4)
      pr_info("emu: WriteFree captured index=%u handle=0x%llx (fifo_a=0x%08x fifo_b=0x%08x)\n",
              index, (unsigned long long)handle, fifo_a, fifo_b);

   emu_free_pool_push(eng, index, handle);
   eng->free_captured++;

   return true;
}

static int emu_process_tx(struct emu_dma_engine *eng)
{
   uint32_t fifo_a, fifo_b, fifo_c, fifo_d;
   uint32_t index, size;
   uint8_t fuser, luser, dest, chan, cont;
   dma_addr_t tx_handle, rx_handle;
   void *tx_virt, *rx_virt;
   uint32_t rx_index;
   uint32_t *ptr;
   uint32_t validity;
   int tx_processed = 0;

   fifo_b = emu_reg_read(eng, EMU_REG_READFIFOB);
   if (fifo_b == 0)
      return 0;

   fifo_a = emu_reg_read(eng, EMU_REG_READFIFOA);
   fifo_c = emu_reg_read(eng, EMU_REG_READFIFOC);
   fifo_d = emu_reg_read(eng, EMU_REG_READFIFOD);

   emu_reg_write(eng, EMU_REG_READFIFOB, 0);

   /* 128-bit descriptor field extraction */
   index     = fifo_c & 0x0FFFFFFF;
   tx_handle = emu_reconstruct_handle(fifo_c, fifo_d);
   size      = fifo_b;
   fuser     = (fifo_a >> 24) & 0xFF;
   luser     = (fifo_a >> 16) & 0xFF;
   dest      = (fifo_a >> 8) & 0xFF;
   chan      = (fifo_a >> 4) & 0xF;
   cont      = (fifo_a >> 3) & 0x1;

   tx_virt = phys_to_virt((phys_addr_t)tx_handle);

   /* ptr[3] doubles as the ring-entry validity word: AxisG2_MapReturn
    * treats ptr[3]==0 as "no entry."  Bits 11:8 = chan, bits 7:0 = dest.
    * Bits 31:12 are unused by the driver, so set bit 16 as a non-zero
    * marker when chan and dest are both zero (e.g., dest 0 loopback). */
   validity = ((chan & 0xF) << 8) | (dest & 0xFF);
   if (validity == 0)
      validity = 0x10000;

   if (!emu_free_pool_pop(eng, &rx_index, &rx_handle)) {
      pr_warn_ratelimited("emu: no free RX buffer for TX loopback (tx_idx=%u)\n",
                          index);

      /* Return TX buffer via rd_ring (128-bit: 4 words) */
      ptr = eng->rd_ring + (eng->rd_ring_idx * 4);
      ptr[0] = fifo_a;
      ptr[1] = index;
      ptr[2] = 0;
      wmb();
      ptr[3] = validity;

      eng->rd_ring_idx = (eng->rd_ring_idx + 1) % eng->addr_count;
      emu_reg_write(eng, EMU_REG_HWRDINDEX, eng->rd_ring_idx);

      eng->tx_count++;
      return 1;
   }

   /* Bound the memcpy by the maxSize register the driver read at init
    * (mirrors the VHDL: the hardware FIFO can't accept oversized TX). A
    * size above EMU_REG_MAXSIZE_INIT means a driver bug — emit a frame-
    * rejection completion (size=0) and skip the copy rather than walk
    * off the end of the RX buffer (the rx pages were allocated for
    * cfgSize <= maxSize). Same return-via-rd_ring shape as the
    * starvation path above keeps AxisG2_MapReturn's invariants intact. */
   if (size > EMU_REG_MAXSIZE_INIT) {
      pr_warn_ratelimited("emu: TX size=%u exceeds maxSize=%u, dropping frame (tx_idx=%u)\n",
                          size, EMU_REG_MAXSIZE_INIT, index);
      emu_free_pool_push(eng, rx_index, rx_handle);

      ptr = eng->rd_ring + (eng->rd_ring_idx * 4);
      ptr[0] = fifo_a;
      ptr[1] = index;
      ptr[2] = 0;
      wmb();
      ptr[3] = validity;

      eng->rd_ring_idx = (eng->rd_ring_idx + 1) % eng->addr_count;
      emu_reg_write(eng, EMU_REG_HWRDINDEX, eng->rd_ring_idx);

      eng->tx_count++;
      return 1;
   }

   rx_virt = phys_to_virt((phys_addr_t)rx_handle);
   memcpy(rx_virt, tx_virt, size);

   /* Write RX completion to write ring (128-bit: 4 words) */
   ptr = eng->wr_ring + (eng->wr_ring_idx * 4);
   ptr[0] = ((uint32_t)fuser << 24) |
            ((uint32_t)luser << 16) |
            ((uint32_t)dest  << 8)  |
            ((uint32_t)cont  << 3);
   ptr[1] = rx_index;
   ptr[2] = size;
   wmb();
   ptr[3] = validity;

   eng->wr_ring_idx = (eng->wr_ring_idx + 1) % eng->addr_count;

   /* Write TX return to read ring (128-bit: 4 words) */
   ptr = eng->rd_ring + (eng->rd_ring_idx * 4);
   ptr[0] = fifo_a;
   ptr[1] = index;
   ptr[2] = size;
   wmb();
   ptr[3] = validity;

   eng->rd_ring_idx = (eng->rd_ring_idx + 1) % eng->addr_count;

   emu_reg_write(eng, EMU_REG_HWWRINDEX, eng->wr_ring_idx);
   emu_reg_write(eng, EMU_REG_HWRDINDEX, eng->rd_ring_idx);

   eng->tx_count++;
   eng->rx_count++;
   tx_processed++;

   return tx_processed;
}

/* ----------------------------------------------------------------
 * IRQ delivery dispatch
 * ---------------------------------------------------------------- */

/*
 * emu_dma_fire_irq - Trigger the IRQ that datadev currently has registered.
 *
 * In INTx mode datadev called request_irq() on emu_irq.virq, so we fire that.
 * In MSI / MSI-X mode, the kernel allocated a child virq through our PCI-MSI
 * domain when datadev called pci_alloc_irq_vectors(); the parent .alloc op
 * stashed it in emu_msi.alloc_virq. Fire that one instead -- emu_irq.virq is
 * stale (datadev never request_irq'd it).
 *
 * The MSI path may briefly have alloc_virq == 0 between emu_dma_start (which
 * runs before the host bridge is created) and the kernel's MSI alloc during
 * datadev probe; emu_msi_fire_safe() short-circuits in that window.
 */
static inline void emu_dma_fire_irq(struct emu_dma_engine *eng)
{
   if (eng->msi != NULL && eng->msi->alloc_virq != 0)
      emu_msi_fire_safe(eng->msi);
   else
      emu_irq_fire_safe(eng->irq);
}

/* ----------------------------------------------------------------
 * Polling kthread
 * ---------------------------------------------------------------- */

static int emu_poll_thread_fn(void *data)
{
   struct emu_dma_engine *eng = data;
   unsigned long init_deadline = 0;
   unsigned long flags;
   uint32_t init_poll_iters = 0;

   while (!kthread_should_stop()) {
      if (!eng->enabled) {
         msleep(10);
         continue;
      }

      /* Re-assert enableVer R/O fields on every cycle. This catches the
       * AxisG2_Clear writel(0x0) that zeroes Desc128En/version during
       * rmmod, before the next insmod's AxisG2_Init re-reads them.
       * Runs unconditionally so the fix works even if fifoReset is
       * never observed (e.g. driver unload without a subsequent load
       * during the poll cycle). */
      emu_enforce_enablever_ro(eng);

      /* Detect online 1->0 edge (AxisG2_Clear).  The driver writes
       * online=0 before fifoReset=1 and before dma_free_coherent, so
       * online is the unambiguous unload signal -- Init never lowers
       * it, and every Clear path writes it.  We use this edge to fully
       * reset engine state that would otherwise require catching the
       * transient fifoReset=1 pulse (driver writes 1/0 back-to-back in
       * microseconds; the 1 ms poll interval often misses the window).
       *
       * Without this reset, free_pool entries from the old driver
       * instance persist into the next one.  On the first TX loopback
       * the emulator may pop a stale (freed) buffer handle, copy into
       * memory the allocator has reassigned, and produce early PRBS
       * mismatches -- which is exactly the CI flake that motivated
       * this hook.
       *
       * Keyed on online (not fifoReset) because AxisG2_Init writes new
       * ring base addrs *before* raising fifoReset; zeroing on that
       * signal races with Init and clobbers valid pointers. */
      {
         uint8_t cur_online = emu_reg_read(eng, EMU_REG_ONLINE) & 0x1;
         if (!cur_online && eng->prev_online) {
            emu_reg_write(eng, EMU_REG_RDBASELOW, 0);
            emu_reg_write(eng, EMU_REG_RDBASEHIGH, 0);
            emu_reg_write(eng, EMU_REG_WRBASELOW, 0);
            emu_reg_write(eng, EMU_REG_WRBASEHIGH, 0);

            spin_lock_irqsave(&eng->free_lock, flags);
            eng->free_count = 0;
            spin_unlock_irqrestore(&eng->free_lock, flags);

            eng->wr_ring_idx = 0;
            eng->rd_ring_idx = 0;
            eng->free_captured = 0;

            eng->rings_valid = false;
            eng->wr_ring = NULL;
            eng->rd_ring = NULL;

            eng->seed_active = false;
            eng->seed_pending = false;

            pr_info("emu: online 1->0 edge, engine state reset\n");
         }
         eng->prev_online = cur_online;
      }

      /* Capture ring addresses if not yet valid */
      if (!eng->rings_valid) {
         emu_capture_rings(eng);
         if (!eng->rings_valid)
            goto do_sleep;

         /* Refresh init_deadline every time rings become valid. On a
          * driver reload, AxisG2_Clear sets fifoReset=1 which starts
          * init_polling with a 100ms deadline *before* the ring regs
          * are re-populated by AxisG2_Init.  insmod takes much longer
          * than 100ms, so by the time rings become valid the deadline
          * has expired — the busy-poll window terminates after a single
          * iteration and the free pool is never refilled. */
         eng->init_polling = true;
         init_deadline = jiffies + msecs_to_jiffies(EMU_INIT_POLL_TIMEOUT_MS);
         init_poll_iters = 0;
         pr_info("emu: rings captured, entering init busy-poll for %dms\n",
                 EMU_INIT_POLL_TIMEOUT_MS);
      }

      /* Detect fifoReset */
      if (emu_reg_read(eng, EMU_REG_FIFORESET) != 0) {
         emu_reg_write(eng, EMU_REG_FIFORESET, 0);

         /* enableVer R/O fields are re-asserted by
          * emu_enforce_enablever_ro() at the top of every poll cycle,
          * so no explicit restore is needed here. */

         spin_lock_irqsave(&eng->free_lock, flags);
         eng->free_count = 0;
         spin_unlock_irqrestore(&eng->free_lock, flags);

         eng->wr_ring_idx = 0;
         eng->rd_ring_idx = 0;
         eng->free_captured = 0;

         /* Drop cached ring pointers so emu_capture_rings() re-reads
          * the BAR base regs on the next iteration.  If the driver
          * rmmod'd (Clear zeroes those regs before dma_free_coherent),
          * rings_valid stays false and the seed phase cannot write
          * zero-size RX completions into freed DMA pages. */
         eng->rings_valid = false;
         eng->wr_ring = NULL;
         eng->rd_ring = NULL;

         eng->seed_active = false;
         eng->seed_pending = false;

         eng->init_polling = true;
         init_deadline = jiffies + msecs_to_jiffies(EMU_INIT_POLL_TIMEOUT_MS);
         init_poll_iters = 0;
         pr_info("emu: fifoReset detected, re-entering init busy-poll\n");

         /* Restart the poll loop so the rings_valid==false check at the
          * top runs (emu_capture_rings re-reads BAR regs).  Falling
          * through would call emu_process_tx which dereferences
          * wr_ring/rd_ring that we just NULL'd. */
         msleep(1);
         continue;
      }

      /* Capture free buffers from writeFifoA/B shadow registers */
      while (emu_capture_free_one(eng)) { }

      if (eng->init_polling)
         init_poll_iters++;

      /* Coalesce forceInt and TX processing into a single IRQ per poll
       * cycle.  The driver's AxisG2_Process drains both rdQueue (TX
       * submission) and wrQueue (WriteFree) in one call.  Firing two
       * IRQs per cycle causes two AxisG2_Process calls, each issuing
       * a WriteFree — but the emulator's shadow register can only
       * hold one value, so the first WriteFree is lost.  Over time
       * this drains the free pool and causes frame drops.
       *
       * TX processing runs even during the seed phase: if the free
       * pool is empty, emu_process_tx returns TX buffers without RX
       * loopback (no deadlock).  Once the seed captures buffers, TX
       * loopback resumes automatically. */
      {
         bool need_irq = false;

         if (emu_reg_read(eng, EMU_REG_FORCEINT) != 0) {
            emu_reg_write(eng, EMU_REG_FORCEINT, 0);
            need_irq = true;
         }

         if (emu_process_tx(eng) > 0)
            need_irq = true;

         if (need_irq) {
            emu_dma_fire_irq(eng);
            eng->irq_count++;
         }
      }

      /* Handle intAckAndEnable */
      if (emu_reg_read(eng, EMU_REG_INTACKENA) != 0) {
         emu_reg_write(eng, EMU_REG_INTACKENA, 0);
         emu_reg_write(eng, EMU_REG_INTENABLE, 1);
      }

      /* Exit init busy-poll after timeout */
      if (eng->init_polling && time_after(jiffies, init_deadline)) {
         eng->init_polling = false;
         pr_info("emu: init busy-poll complete (captured %u free buffers, %u poll iterations)\n",
                 eng->free_captured, init_poll_iters);

         /* If we captured too few RX buffers, activate the seed phase.
          * Write zero-size RX completions one at a time to probe for
          * valid RX buffer indices. The driver returns unowned buffers
          * via WriteFree, which we capture on the next poll cycle. */
         if (eng->rings_valid && eng->free_count < 2) {
            eng->seed_active = true;
            eng->seed_pending = false;
            eng->seed_idx = 0;
            eng->seed_max = EMU_FREE_POOL_MAX;
            eng->seed_target = EMU_FREE_POOL_MAX / 2;
            pr_info("emu: starting RX buffer seed phase (probing indices 0..%u)\n",
                    eng->seed_max - 1);
         }
      }

      /* RX buffer seed phase: write one zero-size completion per cycle,
       * wait for the driver to process it (ptr[3] cleared) before
       * writing the next. This serializes WriteFree responses so the
       * emulator can capture each one from the shadow register. */
      if (eng->seed_active && !eng->init_polling) {
         if (eng->free_count >= eng->seed_target ||
             eng->seed_idx >= eng->seed_max) {
            pr_info("emu: seed phase complete (captured %u free buffers)\n",
                    eng->free_count);
            eng->seed_active = false;
         } else if (eng->seed_pending) {
            /* Check if the driver consumed the previous seed entry */
            uint32_t prev_slot = (eng->wr_ring_idx + eng->addr_count - 1) % eng->addr_count;
            uint32_t *prev = eng->wr_ring + (prev_slot * 4);
            if (prev[3] == 0) {
               eng->seed_pending = false;
               /* WriteFree (if any) was captured at the top of the loop */
            }
         } else {
            /* Write one zero-size RX completion for seed_idx */
            uint32_t *ptr = eng->wr_ring + (eng->wr_ring_idx * 4);
            ptr[0] = 0;                    /* flags: zero */
            ptr[1] = eng->seed_idx;        /* buffer index */
            ptr[2] = 0;                    /* size: zero (triggers DMA_ERR_FIFO) */
            wmb();
            ptr[3] = 0x10000;             /* validity: non-zero, chan=0, dest=0 */

            eng->wr_ring_idx = (eng->wr_ring_idx + 1) % eng->addr_count;
            emu_reg_write(eng, EMU_REG_HWWRINDEX, eng->wr_ring_idx);

            emu_dma_fire_irq(eng);
            eng->irq_count++;
            eng->seed_pending = true;
            eng->seed_idx++;
         }
      }

do_sleep:
      if (eng->init_polling || eng->seed_active)
         usleep_range(100, 200);
      else
         usleep_range(900, 1100);
   }

   return 0;
}

/* ----------------------------------------------------------------
 * Engine lifecycle
 * ---------------------------------------------------------------- */

void emu_dma_set_msi(struct emu_dma_engine *eng, struct emu_msi *msi)
{
   eng->msi = msi;
}

int emu_dma_init(struct emu_dma_engine *eng, struct emu_bar0 *bar,
                 struct emu_irq *irq)
{
   eng->bar = bar;
   eng->irq = irq;
   eng->msi = NULL;
   eng->reg_base = bar->uc_virt + EMU_AGEN2_OFF;

   eng->rd_ring = NULL;
   eng->wr_ring = NULL;
   eng->rd_ring_idx = 0;
   eng->wr_ring_idx = 0;
   eng->addr_count = 0;
   eng->free_count = 0;
   eng->enabled = false;
   eng->rings_valid = false;
   eng->init_polling = false;
   eng->seed_active = false;
   eng->seed_pending = false;
   eng->seed_idx = 0;
   eng->seed_max = 0;
   eng->seed_target = 0;
   eng->tx_count = 0;
   eng->rx_count = 0;
   eng->irq_count = 0;
   eng->free_captured = 0;

   /* enableVer R/O enforcement starts with bit 0 = 0 / enableCnt = 0,
    * matching EMU_REG_ENABLEVER_INIT. The driver's first AxisG2_Enable
    * write to bit 0 will drive a 0->1 edge that advances enable_cnt to 1. */
   eng->prev_enable = 0;
   eng->enable_cnt = 0;

   /* online edge-detection seed matches EMU_REG_ONLINE_INIT = 1.  The
    * driver's AxisG2_Enable will also write 1 (0->1, no action); only
    * AxisG2_Clear's 1->0 write triggers the ring base-reg wipe. */
   eng->prev_online = 1;

   spin_lock_init(&eng->free_lock);

   eng->poll_thread = kthread_create(emu_poll_thread_fn, eng, "emu_dma_poll");
   if (IS_ERR(eng->poll_thread)) {
      int ret = PTR_ERR(eng->poll_thread);
      eng->poll_thread = NULL;
      pr_err("emu: failed to create DMA poll thread (%d)\n", ret);
      return ret;
   }

   pr_info("emu: DMA engine initialized\n");
   return 0;
}

void emu_dma_start(struct emu_dma_engine *eng)
{
   eng->enabled = true;
   wake_up_process(eng->poll_thread);
   pr_info("emu: DMA engine started\n");
}

void emu_dma_stop(struct emu_dma_engine *eng)
{
   eng->enabled = false;

   if (eng->poll_thread) {
      kthread_stop(eng->poll_thread);
      eng->poll_thread = NULL;
   }

   pr_info("emu: DMA engine stopped (tx=%u rx=%u irq=%u free=%u)\n",
           eng->tx_count, eng->rx_count,
           eng->irq_count, eng->free_captured);
}

void emu_dma_destroy(struct emu_dma_engine *eng)
{
   if (eng->poll_thread) {
      kthread_stop(eng->poll_thread);
      eng->poll_thread = NULL;
   }

   pr_info("emu: DMA engine destroyed\n");
}
