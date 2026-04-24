/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    GPU Async engine implementation for the PCI device emulator.
 *
 *    Polls at ~1 ms, mirrors AxiPcieGpuAsyncControl.vhd behaviorally.
 *    See gpu_engine.h for the architecture overview.
 *
 *    BAR0 register access goes through the UC ioremap mapping
 *    (bar->uc_virt) for coherency with the datadev driver's UC mapping
 *    of the same physical pages.  Matches the dma_engine pattern.
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
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/bug.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/err.h>
#include <asm/barrier.h>
#include <linux/mutex.h>

#include "gpu_engine.h"

/* Forward declarations for the PRBS helpers exported by prbs.c.
 * Declared inline here rather than via #include "prbs.h" so this TU
 * is independently compilable in a parallel-wave worktree where prbs.h
 * has not yet landed.  Weak fallbacks immediately below keep modpost
 * happy until prbs.o ships its strong EXPORT_SYMBOL_GPL versions; the
 * weak bodies never execute in the no-lookup (__weak NULL) path so the
 * engine is functionally inert. */
int emu_prbs_gen_data(void *buf, size_t size, u32 sequence);
int emu_prbs_process_data(const void *buf, size_t size);

/* Declared as a non-static module param in emu_main.c; gates the per-frame
 * pr_info in emu_gpu_rx_tick for PRBS monotonicity bring-up. Default 0
 * (off) preserves soak dmesg cleanliness. */
extern uint emu_gpu_debug_sc2;

/* ----------------------------------------------------------------
 * Lazy-binding state for emu_gpu_addr_lookup.
 *
 * The stub's emu_gpu_addr_lookup is resolved via symbol_get on the
 * first tick with real work. have_pin tracks whether we hold a
 * module reference on the stub. Cleared by emu_gpu_drop_pin (invoked
 * from the stub's drain callback in emu_main.c) BEFORE symbol_put.
 * The mutex guarantees the poll thread cannot call into stub text
 * after symbol_put returns.
 * ---------------------------------------------------------------- */
static DEFINE_MUTEX(have_pin_mutex);
static bool have_pin;
typedef void *(*addr_lookup_fn_t)(u64);
static addr_lookup_fn_t addr_lookup_fn;

#define EMU_GPU_MOD "datadev_emulator"

static DEFINE_RATELIMIT_STATE(emu_gpu_rl, HZ, 10);

/* Overridden by nvidia_p2p_stub.  Weak so the emulator loads
 * stand-alone; when the lookup is unresolved, rx/tx ticks no-op. */
__weak void *emu_gpu_addr_lookup(u64 fake_dma)
{
   (void)fake_dma;
   return NULL;
}

/* ----------------------------------------------------------------
 * Lazy-bound wrapper: on first call, symbol_get the stub's
 * emu_gpu_addr_lookup and cache it; subsequent calls reuse the cached
 * pointer under the same mutex. Returns NULL if the stub is not
 * loaded or the lookup fails. Fires pr_info_once on first non-NULL
 * resolution (success criterion #3 probe).
 * ---------------------------------------------------------------- */
static void *emu_gpu_do_addr_lookup(u64 fake_dma)
{
   addr_lookup_fn_t fn;
   void *kva;

   mutex_lock(&have_pin_mutex);
   if (!have_pin) {
      fn = symbol_get(emu_gpu_addr_lookup);
      if (fn) {
         addr_lookup_fn = fn;
         have_pin = true;
      }
   }
   fn = addr_lookup_fn;
   mutex_unlock(&have_pin_mutex);

   if (!fn)
      return NULL;

   kva = fn(fake_dma);
   if (kva) {
      pr_info_once("%s: gpu addr_lookup fake=0x%llx kva=%px (first hit)\n",
                   EMU_GPU_MOD, (unsigned long long)fake_dma, kva);
   }
   return kva;
}

/* Weak PRBS fallbacks -- overridden by prbs.o once the parallel wave
 * merges.  Unreachable in practice because rx/tx ticks only invoke
 * these after a non-NULL emu_gpu_addr_lookup(), which the weak lookup
 * above never returns.  See comment on the forward decls above for
 * rationale. */
__weak int emu_prbs_gen_data(void *buf, size_t size, u32 sequence)
{
   (void)buf;
   (void)size;
   return (int)(sequence + 1);
}

__weak int emu_prbs_process_data(const void *buf, size_t size)
{
   (void)buf;
   (void)size;
   return 0;
}

/* ----------------------------------------------------------------
 * Register helpers
 * ---------------------------------------------------------------- */

static void emu_gpu_read_ctrl(struct emu_gpu_engine *eng,
                              bool *wen, u32 *wcnt,
                              bool *ren, u32 *rcnt)
{
   u32 ctrl = ioread32(eng->reg_base + EMU_GPU_REG_CTRL);
   *wcnt = ctrl & 0x7FFF;
   *wen  = !!(ctrl & BIT(15));
   *rcnt = (ctrl >> 16) & 0x7FFF;
   *ren  = !!(ctrl & BIT(31));
}

static u64 emu_gpu_read_buf_addr(struct emu_gpu_engine *eng,
                                 u32 base, u32 idx)
{
   u32 lo = ioread32(eng->reg_base + base + idx * 8);
   u32 hi = ioread32(eng->reg_base + base + idx * 8 + 4);
   return ((u64)hi << 32) | lo;
}

/* ----------------------------------------------------------------
 * Engine lifecycle
 * ---------------------------------------------------------------- */

int emu_gpu_init(struct emu_gpu_engine *eng, struct emu_bar0 *bar)
{
   if (!eng || !bar || !bar->uc_virt)
      return -EINVAL;

   memset(eng, 0, sizeof(*eng));
   eng->bar = bar;
   /* Use the UC ioremap mapping so ioread32/iowrite32 accesses are
    * coherent with the datadev driver (which also maps BAR0 as UC). */
   eng->reg_base = bar->uc_virt + EMU_GPU_ASYNC_OFF;
   spin_lock_init(&eng->lock);

   return 0;
}

/* ----------------------------------------------------------------
 * RX half-FSM
 * ----------------------------------------------------------------
 *
 * On writeEnable=0: reset nextWriteIdx (VHDL behavior from
 *   `if (r.writeEnable = '0') then v.nextWriteIdx := 0`).
 * On writeCount=0: WARN_ON_ONCE and skip.
 * On freeList[idx]==0: nothing pending at this slot, retry next tick.
 * On emu_gpu_addr_lookup()==NULL: skip with rate-limited info
 *   (never deref NULL).
 * On success: fill PRBS payload @ +DATA_BYTES_C, write size doorbell
 *   at +4, smp_wmb, clear freeList bit, advance cursor, bump rxFrameCnt.
 */
static void emu_gpu_rx_tick(struct emu_gpu_engine *eng)
{
   bool wen, ren;
   u32 wcnt, rcnt;
   unsigned long flags;
   u32 idx, free;
   u64 fake;
   void *kva;
   u32 size, hdr0 = 0;
   u32 cnt;
   int ret;

   emu_gpu_read_ctrl(eng, &wen, &wcnt, &ren, &rcnt);

   spin_lock_irqsave(&eng->lock, flags);

   if (!wen) {
      /* VHDL: nextWriteIdx := 0 when writeEnable = '0'. */
      eng->next_write_idx = 0;
      spin_unlock_irqrestore(&eng->lock, flags);
      return;
   }

   /* wcnt is the 0-based last-valid-index per driver convention
    * (gpu_async.c:291 writes `count-1`; rdmaTest.cu:298 calls
    * setWriteCount(bufCnt-1)). Number of active buffers = wcnt + 1;
    * iterate indices 0..wcnt. wcnt == 0 is a legitimate single-buffer
    * configuration, not an error. */
   idx = eng->next_write_idx % (wcnt + 1);
   free = ioread32(eng->reg_base + EMU_GPU_FREELIST_BASE + idx * 4);
   if (free == 0) {
      /* No pending free-list entry at this slot; try again next tick. */
      spin_unlock_irqrestore(&eng->lock, flags);
      return;
   }

   fake = emu_gpu_read_buf_addr(eng, EMU_GPU_WRADDR_BASE, idx);

   /* Drop spinlock before emu_gpu_do_addr_lookup: that function takes
    * have_pin_mutex (a sleeping lock) to lazily bind the stub symbol.
    * Holding a spinlock across mutex_lock is illegal under
    * CONFIG_DEBUG_ATOMIC_SLEEP.  We have already read idx and fake_dma
    * from the register window; re-validate free after reacquiring. */
   spin_unlock_irqrestore(&eng->lock, flags);

   kva = emu_gpu_do_addr_lookup(fake);
   if (!kva) {
      /* Never deref a NULL lookup. */
      if (__ratelimit(&emu_gpu_rl))
         pr_info("%s: gpu rx: addr lookup NULL for fake=0x%llx idx=%u\n",
                 EMU_GPU_MOD, (unsigned long long)fake, idx);
      return;
   }

   /* Frame size: honor RemoteWriteMaxSize; default to 4096 if zero. */
   size = ioread32(eng->reg_base + EMU_GPU_REG_RW_MAX_SIZE);
   if (size == 0)
      size = 4096;
   if (size > 65536)
      size = 65536;

   /* Fill PRBS payload at kva + DATA_BYTES_C. Per prbs.h's documented
    * contract, success returns (int)(sequence + 1u) which can itself
    * read as negative when sequence >= 0x7FFFFFFE (random prbs seed);
    * only -EINVAL (-22) is a true error signal. Guard specifically. */
   ret = emu_prbs_gen_data((u8 *)kva + EMU_GPU_DATA_BYTES_C,
                           size, eng->rx_prbs_seq);
   if (ret == -EINVAL) {
      if (__ratelimit(&emu_gpu_rl))
         pr_info("%s: gpu rx: prbs gen failed idx=%u size=%u ret=%d\n",
                 EMU_GPU_MOD, idx, size, ret);
      return;
   }

   /* Descriptor layout (V4, per rdmaTest.cu:327 -- GPU polls +4):
    *   [+0..+3] : AxiWrDesc64_t header word (reserved)
    *   [+4..+7] : size (doorbell; non-zero completes cuStreamWaitValue32)
    */
   memcpy((u8 *)kva + 0, &hdr0, sizeof(hdr0));
   memcpy((u8 *)kva + 4, &size, sizeof(size));

   /* Payload + header must be visible before the free-list clear. */
   smp_wmb();

   /* Reacquire spinlock for state mutation (free-list ack, cursor advance,
    * counter bump).  Re-check free to guard against a concurrent tick that
    * may have processed this same slot during the lock-dropped window. */
   spin_lock_irqsave(&eng->lock, flags);
   free = ioread32(eng->reg_base + EMU_GPU_FREELIST_BASE + idx * 4);
   if (free != 0) {
      /* Clear the free-list bit (ack this slot). */
      iowrite32(0, eng->reg_base + EMU_GPU_FREELIST_BASE + idx * 4);

      /* Advance round-robin cursor. wcnt is 0-based idx max, so the
       * wrap width is wcnt + 1 (matches the read at line 218). */
      eng->next_write_idx = (idx + 1) % (wcnt + 1);

      /* Bump rxFrameCnt (RMW under the engine lock). */
      eng->rx_prbs_seq = (u32)ret;

      /* Per-frame RX instrumentation. Gated on emu_gpu_debug_sc2
       * (module_param in emu_main.c, default 0). Literal prefix
       * "emu_gpu: " is load-bearing — the shell wrapper greps for it.
       * No rate limiting. pr_info under spinlock is safe per kernel
       * conventions. */
      if (emu_gpu_debug_sc2)
         pr_info("emu_gpu: buf=%u rx_seq=0x%08x\n",
                 idx, eng->rx_prbs_seq);

      cnt = ioread32(eng->reg_base + EMU_GPU_REG_RX_FRAME_CNT);
      iowrite32(cnt + 1, eng->reg_base + EMU_GPU_REG_RX_FRAME_CNT);
   }
   spin_unlock_irqrestore(&eng->lock, flags);
}

/* ----------------------------------------------------------------
 * TX half-FSM
 * ----------------------------------------------------------------
 *
 * On readEnable=0: reset nextReadIdx (VHDL disable reset).
 * On readCount=0: WARN_ON_ONCE and skip.
 * On readReqSize[idx]==0: nothing pending at this slot, retry next tick.
 * On emu_gpu_addr_lookup()==NULL: skip with rate-limited info
 *   (never deref NULL).
 * On success: call emu_prbs_process_data on payload @ +DATA_BYTES_C
 *   (log but do not abort on PRBS mismatch so the ack still fires),
 *   write 1 to kva+0 (TX ACK doorbell -- rdmaTest.cu
 *   cuStreamWaitValue32(txBuffs, 1, GEQ)), smp_wmb, clear request word,
 *   advance cursor mod rcnt, bump txFrameCnt.
 */
static void emu_gpu_tx_tick(struct emu_gpu_engine *eng)
{
   bool wen, ren;
   u32 wcnt, rcnt;
   unsigned long flags;
   u32 idx, reqsz;
   u64 fake;
   void *kva;
   u32 cnt;
   u32 one = 1;
   int ret;

   emu_gpu_read_ctrl(eng, &wen, &wcnt, &ren, &rcnt);

   spin_lock_irqsave(&eng->lock, flags);

   if (!ren) {
      /* VHDL: nextReadIdx := 0 when readEnable = '0'. */
      eng->next_read_idx = 0;
      spin_unlock_irqrestore(&eng->lock, flags);
      return;
   }

   /* rcnt is the 0-based last-valid-index (matches wcnt semantics).
    * Number of active read buffers = rcnt + 1; iterate 0..rcnt. */
   idx = eng->next_read_idx % (rcnt + 1);
   reqsz = ioread32(eng->reg_base + EMU_GPU_READREQ_BASE + idx * 4);
   if (reqsz == 0) {
      /* No pending read request at this slot. */
      spin_unlock_irqrestore(&eng->lock, flags);
      return;
   }

   fake = emu_gpu_read_buf_addr(eng, EMU_GPU_RDADDR_BASE, idx);

   /* Drop spinlock before emu_gpu_do_addr_lookup: that function takes
    * have_pin_mutex (a sleeping lock).  Holding a spinlock across
    * mutex_lock violates CONFIG_DEBUG_ATOMIC_SLEEP.  We have already
    * read idx, reqsz, and fake_dma; re-validate reqsz after reacquire. */
   spin_unlock_irqrestore(&eng->lock, flags);

   kva = emu_gpu_do_addr_lookup(fake);
   if (!kva) {
      /* Never deref a NULL lookup (TX side). */
      if (__ratelimit(&emu_gpu_rl))
         pr_info("%s: gpu tx: addr lookup NULL for fake=0x%llx idx=%u\n",
                 EMU_GPU_MOD, (unsigned long long)fake, idx);
      return;
   }

   /* Verify egress PRBS payload.  Mismatches are logged (rate-limited)
    * but do NOT abort the ack: CI sees the PRBS verdict through the
    * test binary's own checker; the ack protocol must advance in
    * lock-step with the request count regardless of verify outcome. */
   ret = emu_prbs_process_data((u8 *)kva + EMU_GPU_DATA_BYTES_C, reqsz);
   if (ret < 0 && __ratelimit(&emu_gpu_rl))
      pr_info("%s: gpu tx: prbs verify failed idx=%u size=%u ret=%d\n",
              EMU_GPU_MOD, idx, reqsz, ret);

   /* TX ACK doorbell: write 1 at remoteReadAddr + 0.
    * rdmaTest.cu:339 waits cuStreamWaitValue32(txBuffs, 1, GEQ). */
   memcpy((u8 *)kva, &one, sizeof(one));
   smp_wmb();

   /* Reacquire spinlock for state mutation (request-word clear, cursor
    * advance, counter bump).  Re-check reqsz to guard against a
    * concurrent tick that may have processed this slot during the
    * lock-dropped window. */
   spin_lock_irqsave(&eng->lock, flags);
   reqsz = ioread32(eng->reg_base + EMU_GPU_READREQ_BASE + idx * 4);
   if (reqsz != 0) {
      /* Clear the request word (ack this slot). */
      iowrite32(0, eng->reg_base + EMU_GPU_READREQ_BASE + idx * 4);

      /* Advance round-robin cursor. rcnt is 0-based idx max, so the
       * wrap width is rcnt + 1 (matches the read at line 344). */
      eng->next_read_idx = (idx + 1) % (rcnt + 1);

      /* Bump txFrameCnt. No per-direction PRBS sequence is tracked here:
       * emu_prbs_process_data is stateless and extracts the expected
       * sequence from the frame header itself (data32[0]), so there is
       * nothing meaningful to accumulate in the engine struct. */
      cnt = ioread32(eng->reg_base + EMU_GPU_REG_TX_FRAME_CNT);
      iowrite32(cnt + 1, eng->reg_base + EMU_GPU_REG_TX_FRAME_CNT);
   }
   spin_unlock_irqrestore(&eng->lock, flags);
}

/* ----------------------------------------------------------------
 * Poll kthread -- alternates rx_tick / tx_tick at ~1 ms.
 * ---------------------------------------------------------------- */

/* CntRst handling: userspace (GpuAsyncCoreRegs::countReset) writes
 * any non-zero value to register 0x20 to request a frame-counter
 * clear. The real VHDL performs the reset combinationally; the
 * emulator services it lazily here on each poll-thread tick. */
static void emu_gpu_service_cnt_rst(struct emu_gpu_engine *eng)
{
   u32 rst = ioread32(eng->reg_base + EMU_GPU_REG_CNT_RST);
   if (rst) {
      iowrite32(0, eng->reg_base + EMU_GPU_REG_RX_FRAME_CNT);
      iowrite32(0, eng->reg_base + EMU_GPU_REG_TX_FRAME_CNT);
      iowrite32(0, eng->reg_base + EMU_GPU_REG_CNT_RST);
   }
}

/* Poll interval is tunable via the emu_gpu_poll_interval_us module param
 * (emu_main.c). Default 1000 µs keeps developer workflow cheap; CI runners
 * under nested-KVM contention set a tighter value (e.g. 200 µs) so the
 * kthread keeps up when the host scheduler oversleeps usleep_range. */
extern uint emu_gpu_poll_interval_us;

static int emu_gpu_poll_thread_fn(void *data)
{
   struct emu_gpu_engine *eng = data;
   unsigned int us_min, us_max;

   pr_info("%s: gpu engine poll thread starting (interval=%u us)\n",
           EMU_GPU_MOD, emu_gpu_poll_interval_us);
   while (!kthread_should_stop()) {
      emu_gpu_service_cnt_rst(eng);
      emu_gpu_rx_tick(eng);
      emu_gpu_tx_tick(eng);
      /* Read the param every tick so late-binding sysfs writes take
       * effect without a reload. ±10% jitter tolerance for usleep_range. */
      us_min = emu_gpu_poll_interval_us;
      if (us_min < 10)
         us_min = 10;           /* floor: avoid 100% CPU spin */
      us_max = us_min + (us_min / 10) + 1;
      usleep_range(us_min, us_max);
   }
   pr_info("%s: gpu engine poll thread stopping\n", EMU_GPU_MOD);
   return 0;
}

/* ----------------------------------------------------------------
 * Lifecycle -- start / stop / destroy
 * ---------------------------------------------------------------- */

int emu_gpu_start(struct emu_gpu_engine *eng)
{
   struct task_struct *t;

   if (!eng || !eng->reg_base)
      return -EINVAL;

   /* Idempotent: already running is success. */
   if (eng->poll_thread)
      return 0;

   eng->enabled = true;
   t = kthread_run(emu_gpu_poll_thread_fn, eng, "emu_gpu_poll");
   if (IS_ERR(t)) {
      int err = PTR_ERR(t);

      eng->enabled = false;
      eng->poll_thread = NULL;
      pr_err("%s: failed to start gpu poll thread (%d)\n",
             EMU_GPU_MOD, err);
      return err;
   }

   /* Promote to SCHED_FIFO(1). The emulator's correctness depends on
    * the poll thread ticking at roughly `emu_gpu_poll_interval_us`
    * cadence, but usleep_range is subject to scheduler oversleep
    * under CFS contention — observed 10+ seconds on GHA nested-KVM
    * runners when the test binary is busy-waiting on a doorbell in
    * userspace. SCHED_FIFO gets the wakeup honored promptly because
    * RT tasks preempt CFS. Priority 1 ("fifo_low") is low enough
    * that the kernel's own critical RT tasks still dominate. The
    * sleep inside the loop remains the CPU-relief valve. */
   sched_set_fifo_low(t);

   eng->poll_thread = t;
   return 0;
}

void emu_gpu_stop(struct emu_gpu_engine *eng)
{
   if (!eng)
      return;
   if (eng->poll_thread) {
      kthread_stop(eng->poll_thread);
      eng->poll_thread = NULL;
   }
   eng->enabled = false;
}

void emu_gpu_destroy(struct emu_gpu_engine *eng)
{
   /* Thread is expected to already be stopped by the lifecycle in
    * emu_main.c (emu_gpu_stop before destroy).  Still safe if called
    * twice or without a prior stop. */
   if (!eng)
      return;
   if (eng->poll_thread) {
      kthread_stop(eng->poll_thread);
      eng->poll_thread = NULL;
   }
}

/* ----------------------------------------------------------------
 * Drop the stub pin. Called from emu_main.c:emu_on_stub_empty
 * (drain callback) when the stub's address table transitions to
 * empty, and defensively from emu_exit.
 *
 * Ordering (Pitfall B): NULL the function pointer FIRST so any racing
 * poll tick finishing mutex_lock after this point sees fn==NULL and
 * bails without calling into stub text. Only THEN call symbol_put,
 * which releases the module reference and makes rmmod of the stub
 * possible. The have_pin flag guarantees idempotency — calling
 * drop_pin twice does not double-put.
 * ---------------------------------------------------------------- */
void emu_gpu_drop_pin(void)
{
   mutex_lock(&have_pin_mutex);
   addr_lookup_fn = NULL;
   if (have_pin) {
      have_pin = false;
      symbol_put(emu_gpu_addr_lookup);
   }
   mutex_unlock(&have_pin_mutex);
}

EXPORT_SYMBOL_GPL(emu_gpu_init);
EXPORT_SYMBOL_GPL(emu_gpu_start);
EXPORT_SYMBOL_GPL(emu_gpu_stop);
EXPORT_SYMBOL_GPL(emu_gpu_destroy);
