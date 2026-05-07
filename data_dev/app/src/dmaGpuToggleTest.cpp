/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    Mid-stream state-transition test for the GPU-async data path emulator.
 *    Three subtests:
 *      1. toggle_disable: setWriteEnable(0)+setReadEnable(0) mid-run;
 *         100ms quiesce + countReset + 100ms settle; assert
 *         rxFrameCnt==0 && txFrameCnt==0 (race-immune zero baseline).
 *      2. toggle_resume:  setWriteEnable(1)+setReadEnable(1); run 4 frames
 *         (one full round-robin at bufCnt=4); assert PRBS clean on first frame.
 *      3. maxbuffers_4to2: while traffic flowing, writeCount(1)+readCount(1)
 *         (reduces 4->2 active buffers); run 100 more frames; assert no
 *         dmesg WARN/KASAN and no PRBS mismatch.
 *
 *    Exit code: 0 if all subtests pass, 1 otherwise.
 *    Assumes modules pre-loaded (matches tests/test_gpu_ioctls.sh precedent).
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

/* System headers */
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <argp.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <atomic>
#include <chrono>

/* Project headers */
#include <DmaDriver.h>
#include <GpuAsync.h>
#include <GpuAsyncRegs.h>
#include <GpuAsyncUser.h>
#include <PrbsData.h>
#include "emu_gpu_addr_table.h"

/* ---------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------*/

static constexpr char const *kStubDev         = "/dev/nvidia_p2p_stub_mem";
static constexpr int         kBufCnt          = 4;
static constexpr uint32_t    kBufSize         = 65536;
static constexpr int         kPreambleFrames  = 100;
static constexpr int         kResumeFrames    = 4;
static constexpr int         kPostReduceFrames = 100;
static constexpr int         kPollDeadlineSec = 10;

/*
 * AxiWrDesc64_t: 128-bit frame header at buffer offset 0.
 * size field at +4 is the doorbell. Payload starts at +16.
 */
typedef struct {
   uint32_t header;
   uint32_t size;
   uint8_t  pad[8];
} AxiWrDesc64_t;
static_assert(sizeof(AxiWrDesc64_t) == 16, "AxiWrDesc64_t must be 16 bytes");

/* ---------------------------------------------------------------------------
 * argp CLI
 * -------------------------------------------------------------------------*/

const char *argp_program_version     = "dmaGpuToggleTest 1.0";
const char *argp_program_bug_address = "ruckman@stanford.edu";

struct Args {
   const char *path;
};

static struct argp_option options[] = {
   {"path", 'p', "PATH", 0, "Device node path (default: /dev/datadev_0)", 0},
   {0, 0, 0, 0, 0, 0}
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
   struct Args *a = reinterpret_cast<struct Args *>(state->input);
   switch (key) {
      case 'p': a->path = arg; break;
      default:  return ARGP_ERR_UNKNOWN;
   }
   return 0;
}

static struct argp argp_parser = { options, parse_opt, 0, 0, 0, 0, 0 };

/* ---------------------------------------------------------------------------
 * Pass/fail counters + report helper
 * -------------------------------------------------------------------------*/

static int gPassed = 0;
static int gErrors = 0;

/* ---------------------------------------------------------------------------
 * atexit cleanup state (mirrors rdmaTestEmu.cpp:126-144).
 *
 * Without this, a timeout inside emu_wait_value32_with_deadline calls
 * std::exit(1) directly from deep in runOneFrame and bypasses the main()
 * teardown block. That leaves entries in the nvidia_p2p_stub hashtable,
 * which fires WARN_ON(!hash_empty(addr_ht)) at
 * emu_gpu_addr_table_exit+0x32 on the next rmmod cycle — observed in
 * GHA run 24894533793 (debian:experimental toggle flake).
 *
 * main() arms these after opening fd/stub_fd and clears s_atexit_armed
 * on the happy-path teardown so the handler runs exactly once.
 * -------------------------------------------------------------------------*/
static int  s_atexit_fd      = -1;
static int  s_atexit_stub_fd = -1;
static bool s_atexit_armed   = false;

static void toggle_test_atexit(void) {
   if (!s_atexit_armed) return;
   s_atexit_armed = false;                 /* idempotent: only run once */
   if (s_atexit_fd >= 0) {
      /* Best-effort unregister; ignore errors (kernel may have already
       * dropped state if the datadev module was unloaded before exit). */
      (void)gpuRemNvidiaMemory(s_atexit_fd);
      ::close(s_atexit_fd);
      s_atexit_fd = -1;
   }
   if (s_atexit_stub_fd >= 0) {
      ::close(s_atexit_stub_fd);
      s_atexit_stub_fd = -1;
   }
}

static void report(const char *name, bool ok, const char *detail) {
   if (ok) {
      printf("[PASS] %-40s %s\n", name, detail ? detail : "");
      gPassed++;
   } else {
      printf("[FAIL] %-40s %s\n", name, detail ? detail : "");
      gErrors++;
   }
}

/* ---------------------------------------------------------------------------
 * Buffer helpers (inline duplicate of rdmaTestEmu.cpp:175-234)
 *
 * gpuEmuAllocBuf reserves a stub buffer via STUB_RESERVE_BUF and mmaps it
 * with 64KB alignment (required by the emulator's addr_lookup which keys on
 * 64KB boundaries).
 * -------------------------------------------------------------------------*/

static void *gpuEmuAllocBuf(int stub_fd, size_t size, uint32_t *out_buf_id) {
   struct stub_reserve_req req;
   std::memset(&req, 0, sizeof(req));
   req.size = static_cast<uint32_t>(size);

   if (::ioctl(stub_fd, STUB_RESERVE_BUF, &req) < 0) {
      fprintf(stderr,
         "dmaGpuToggleTest: STUB_RESERVE_BUF failed size=%zu: %s\n",
         size, std::strerror(errno));
      return nullptr;
   }

   long page_size = ::sysconf(_SC_PAGESIZE);
   if (page_size <= 0) page_size = 4096;
   off_t offset = static_cast<off_t>(req.buf_id) * page_size;

   constexpr size_t kAlign = 64 * 1024;
   void *rsv = ::mmap(nullptr, size + kAlign, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   if (rsv == MAP_FAILED) {
      fprintf(stderr,
         "dmaGpuToggleTest: mmap reservation size=%zu failed: %s\n",
         size + kAlign, std::strerror(errno));
      return nullptr;
   }

   uintptr_t rsv_u   = reinterpret_cast<uintptr_t>(rsv);
   uintptr_t aligned = (rsv_u + kAlign - 1) & ~(uintptr_t)(kAlign - 1);
   size_t    head    = aligned - rsv_u;
   size_t    tail    = kAlign - head;

   if (head > 0) ::munmap(rsv, head);
   if (tail > 0) ::munmap(reinterpret_cast<void *>(aligned + size), tail);

   void *va = ::mmap(reinterpret_cast<void *>(aligned), size,
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_FIXED, stub_fd, offset);
   if (va == MAP_FAILED) {
      fprintf(stderr,
         "dmaGpuToggleTest: mmap(buf_id=%u size=%zu) failed: %s\n",
         req.buf_id, size, std::strerror(errno));
      /* Release the aligned reservation we already trimmed; the head/tail
       * munmaps above only freed the slack, leaving `size` bytes of
       * PROT_NONE address space orphaned if we return here without it. */
      ::munmap(reinterpret_cast<void *>(aligned), size);
      return nullptr;
   }

   *out_buf_id = req.buf_id;
   return va;
}

static void gpuEmuFreeBuf(void *va, size_t size) {
   if (va && va != MAP_FAILED) ::munmap(va, size);
}

/* ---------------------------------------------------------------------------
 * Acquire-load + PAUSE spin with deadline
 *
 * Polls a kernel-written 32-bit doorbell with std::memory_order_acquire
 * to pair with the emulator RX/TX tick's smp_wmb(). On timeout prints
 * a diagnostic and calls std::exit(1).
 * -------------------------------------------------------------------------*/

static uint32_t emu_wait_value32_with_deadline(
      volatile uint32_t *addr, uint32_t target,
      std::chrono::seconds budget, const char *tag, uint32_t buf) {
   auto start = std::chrono::steady_clock::now();
   auto *a    = reinterpret_cast<std::atomic<uint32_t> *>(
                     const_cast<uint32_t *>(addr));
   unsigned spins = 0;
   for (;;) {
      uint32_t v = a->load(std::memory_order_acquire);
      if (v >= target) return v;
      __builtin_ia32_pause();
      if ((++spins & 0x3FF) == 0) {
         auto now = std::chrono::steady_clock::now();
         if (now - start >= budget) {
            auto secs = std::chrono::duration_cast<
                  std::chrono::duration<double>>(now - start).count();
            fprintf(stderr,
               "dmaGpuToggleTest: %s timeout buf=%u elapsed=%.1fs\n",
               tag, buf, secs);
            std::exit(1);
         }
      }
   }
}

/* ---------------------------------------------------------------------------
 * runOneFrame: execute one complete RX->TX round-trip on buffer curBuff.
 *
 * Follows the same 8-step sequence as rdmaTestEmu.cpp:331-397.
 * Returns 0 on success, 1 on timeout (std::exit called internally on
 * deadline expiry, so return 1 only if a non-fatal error is detected).
 * When verify=true the shared PrbsData instance verifies the payload.
 *
 * IMPORTANT: does NOT call regs.countReset() — the subtest-level
 * quiesce-and-reset in toggle_disable owns the counter-reset lifecycle;
 * runOneFrame must not interfere with that baseline.
 * -------------------------------------------------------------------------*/

static int runOneFrame(GpuAsyncCoreRegs &regs, int bufCnt, int curBuff,
                       uint8_t **rxBuffs, uint8_t **txBuffs,
                       uint32_t bufSize, PrbsData *prbs, bool verify) {
   (void)bufCnt;
   const uint32_t dmaHeaderSize = regs.dmaDataBytes();

   /* (1) Wait for FPGA's RX doorbell on buffer curBuff (10s deadline). */
   emu_wait_value32_with_deadline(
      reinterpret_cast<uint32_t *>(rxBuffs[curBuff] + 4), 1,
      std::chrono::seconds(kPollDeadlineSec),
      "rx doorbell", static_cast<uint32_t>(curBuff));

   /* (2) Read header. Acquire load above synchronizes-with emulator smp_wmb. */
   AxiWrDesc64_t hdr;
   std::memcpy(&hdr, rxBuffs[curBuff], sizeof(hdr));

   /* Bound-check hdr.size before memcpy (defense-in-depth).
    * Guard the subtraction first: if dmaHeaderSize > bufSize (e.g. stale/
    * corrupted BAR0 read during an insmod race), bufSize - dmaHeaderSize
    * would underflow as uint32_t and silently accept any hdr.size. */
   if (dmaHeaderSize > bufSize || hdr.size > bufSize - dmaHeaderSize) {
      fprintf(stderr,
         "dmaGpuToggleTest: invalid hdr.size=%u bufSize=%u dmaHeaderSize=%u\n",
         hdr.size, bufSize, dmaHeaderSize);
      std::exit(1);
   }

   /* (3) PRBS verify on the first frame of each phase when requested.
    * Single shared PrbsData instance continues across subtest boundaries
    * (Pitfall G: kernel emu_prbs_* state is continuous across the toggle). */
   if (verify && prbs != nullptr) {
      if (!prbs->processData(rxBuffs[curBuff] + dmaHeaderSize, hdr.size)) {
         fprintf(stderr,
            "dmaGpuToggleTest: PRBS mismatch size=%u buf=%d\n",
            hdr.size, curBuff);
         return 1;
      }
   }

   /* (4) Wait for emulator TX-side ack at txBuffs[curBuff] (10s). */
   emu_wait_value32_with_deadline(
      reinterpret_cast<uint32_t *>(txBuffs[curBuff]), 1,
      std::chrono::seconds(kPollDeadlineSec),
      "tx ack", static_cast<uint32_t>(curBuff));

   /* (5) memcpy payload RX->TX. */
   std::memcpy(txBuffs[curBuff] + dmaHeaderSize,
               rxBuffs[curBuff] + dmaHeaderSize, hdr.size);

   /* (6) Clear GPU-side TX free-list slot. */
   *reinterpret_cast<volatile uint32_t *>(txBuffs[curBuff]) = 0;

   /* (7) Write remoteReadSize (FPGA doorbell for TX tick). */
   regs.writeReg(regs.remoteReadSizeOffset(curBuff), hdr.size);

   /* (8) Clear GPU-side RX doorbell; re-arm FPGA free-list slot. */
   *reinterpret_cast<volatile uint32_t *>(rxBuffs[curBuff] + 4) = 0;
   regs.writeReg(regs.freeListOffset(curBuff), 1);

   return 0;
}

/* ---------------------------------------------------------------------------
 * armBuffers: stop engine, arm all doorbells and free-list slots, set
 * RemoteWriteMaxSize, bring engine back up.
 * -------------------------------------------------------------------------*/

static void armBuffers(GpuAsyncCoreRegs &regs, int bufCnt,
                       uint8_t **rxBuffs, uint8_t **txBuffs,
                       uint32_t bufSize) {
   const uint32_t dmaHeaderSize = regs.dmaDataBytes();

   /* Quiesce-and-reset: the sequential setWriteEnable(0) +
    * setReadEnable(0) pair is NOT atomic vs the 1ms emu_gpu_poll kthread,
    * so wait 100ms for any in-flight tick to drain, call countReset() to
    * establish a zero baseline for both counters, then wait another 100ms
    * to confirm no further motion before re-arming. */
   regs.setWriteEnable(0);
   regs.setReadEnable(0);
   ::usleep(100000);          /* let kthread drain any pending tick */
   regs.countReset();         /* establish zero baseline */
   ::usleep(100000);          /* confirm no further counter motion */

   regs.setWriteCount(bufCnt - 1);
   regs.setReadCount(bufCnt - 1);

   for (int i = 0; i < bufCnt; ++i) {
      regs.writeReg(regs.freeListOffset(i), 1);
      *reinterpret_cast<volatile uint32_t *>(rxBuffs[i] + 4) = 0;
      *reinterpret_cast<volatile uint32_t *>(txBuffs[i])     = 1;
   }

   regs.setRemoteWriteMaxSize(0, bufSize - dmaHeaderSize);

   regs.setWriteEnable(1);
   regs.setReadEnable(1);
}

/* ---------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/

int main(int argc, char **argv) {
   struct Args args;
   args.path = "/dev/datadev_0";
   argp_parse(&argp_parser, argc, argv, 0, 0, &args);

   /* Open datadev. */
   int fd = ::open(args.path, O_RDWR);
   if (fd < 0) {
      fprintf(stderr, "dmaGpuToggleTest: open(%s) failed: %s\n",
              args.path, std::strerror(errno));
      return 1;
   }

   /* Open stub miscdevice for buffer reservations. */
   int stub_fd = ::open(kStubDev, O_RDWR);
   if (stub_fd < 0) {
      fprintf(stderr, "dmaGpuToggleTest: open(%s) failed: %s\n",
              kStubDev, std::strerror(errno));
      ::close(fd);
      return 1;
   }

   /* Arm atexit cleanup BEFORE any allocation so a timeout inside
    * emu_wait_value32_with_deadline (which calls std::exit(1) from deep in
    * runOneFrame) still drops the stub hashtable entries via
    * gpuRemNvidiaMemory. Without this the next rmmod fires
    * WARN_ON(!hash_empty(addr_ht)) at emu_gpu_addr_table_exit+0x32. */
   s_atexit_fd      = fd;
   s_atexit_stub_fd = stub_fd;
   s_atexit_armed   = true;
   if (std::atexit(toggle_test_atexit) != 0) {
      fprintf(stderr, "dmaGpuToggleTest: atexit registration failed\n");
      s_atexit_armed = false;
      ::close(stub_fd);
      ::close(fd);
      return 1;
   }

   /* Map BAR0 GPU async region. */
   void *bar0 = dmaMapRegister(fd, GPU_ASYNC_CORE_OFFSET, GPU_ASYNC_CORE_SIZE);
   if (!bar0) {
      fprintf(stderr, "dmaGpuToggleTest: dmaMapRegister failed\n");
      ::close(stub_fd);
      ::close(fd);
      return 1;
   }

   GpuAsyncCoreRegs regs(bar0);

   printf("=== dmaGpuToggleTest: %s ===\n", args.path);

   /* Defensive cleanup of stale BAR0 per-buffer state from a prior
    * test instance. test_gpu_dma_loopback.sh's run_with_retry harness
    * re-invokes this binary on a 10s tx-ack timeout flake, but the
    * prior instance's gpuRemNvidiaMemory only clears ctrl and
    * RW_MAX_SIZE — remoteReadSize[] and freeList[] survive. On the
    * next gpuAddNvidiaMemory(write=0) below, ctrl re-enables RE=1
    * with rcnt=0; the kernel TX tick immediately reads the stale
    * remoteReadSize[0]=65520, looks up the freshly-mapped (zero-
    * filled) TX buffer, and emu_prbs_process_data fires
    * WARN_ON_ONCE at prbs.c:123 because the new buffer's
    * data32[1]=0 yields event_length=4 != stale reqsz=65520. Force
    * a clean slate here. ctrl was already cleared to WE=0/RE=0 by
    * the prior session's Gpu_RemNvidia, so no engine activity can
    * race these writes. */
   regs.setWriteEnable(0);
   regs.setReadEnable(0);
   {
      const uint32_t maxBuffersHw = regs.maxBuffers();
      for (uint32_t i = 0; i < maxBuffersHw; ++i) {
         regs.setRemoteReadSize(i, 0);
         regs.writeReg(regs.freeListOffset(i), 0);
      }
   }

   /* Allocate rx/tx buffer arrays. */
   uint8_t  *rxBuffs[kBufCnt] = {nullptr};
   uint8_t  *txBuffs[kBufCnt] = {nullptr};
   uint32_t  rxBufIds[kBufCnt] = {0};
   uint32_t  txBufIds[kBufCnt] = {0};
   int rxAllocCnt = 0, txAllocCnt = 0;

   for (int i = 0; i < kBufCnt; ++i) {
      void *va = gpuEmuAllocBuf(stub_fd, kBufSize, &rxBufIds[i]);
      if (!va) goto cleanup_fail;
      rxBuffs[i] = static_cast<uint8_t *>(va);
      rxAllocCnt++;

      if (gpuAddNvidiaMemory(fd, 1,
            reinterpret_cast<uint64_t>(rxBuffs[i]), kBufSize) < 0) {
         fprintf(stderr,
            "dmaGpuToggleTest: gpuAddNvidiaMemory(write=1, buf=%d) failed: %s\n",
            i, std::strerror(errno));
         goto cleanup_fail;
      }

      va = gpuEmuAllocBuf(stub_fd, kBufSize, &txBufIds[i]);
      if (!va) goto cleanup_fail;
      txBuffs[i] = static_cast<uint8_t *>(va);
      txAllocCnt++;

      if (gpuAddNvidiaMemory(fd, 0,
            reinterpret_cast<uint64_t>(txBuffs[i]), kBufSize) < 0) {
         fprintf(stderr,
            "dmaGpuToggleTest: gpuAddNvidiaMemory(write=0, buf=%d) failed: %s\n",
            i, std::strerror(errno));
         goto cleanup_fail;
      }
   }

   /* Reset counters and bring the engine up. */
   regs.countReset();
   armBuffers(regs, kBufCnt, rxBuffs, txBuffs, kBufSize);

   /*
    * Single shared PrbsData instance across all subtests.
    * Constructed BEFORE preamble traffic; NOT reconstructed between subtests
    * (Pitfall G: kernel emu_prbs_* state is continuous across the toggle,
    * so userspace verifier must continue without reset).
    */
   {
      PrbsData prbs(32, 4, 1, 2, 6, 31);

      /* ---- Preamble: run kPreambleFrames frames to establish traffic. ----
       *
       * verify=true on EVERY frame so userspace PrbsData._sequence tracks
       * kernel eng->rx_prbs_seq byte-for-byte. Without this, _sequence
       * would advance only on n==0 while the kernel advances every frame,
       * leaving _sequence (kPreambleFrames-1) frames behind by the end of
       * the preamble and causing the toggle_resume first-frame verify to
       * fail with "PRBS mismatch size=65520 buf=0". */
      int curBuff = 0;
      for (int n = 0; n < kPreambleFrames; ++n) {
         if (runOneFrame(regs, kBufCnt, curBuff, rxBuffs, txBuffs, kBufSize,
                         &prbs, /*verify=*/true) != 0) {
            fprintf(stderr, "dmaGpuToggleTest: preamble frame %d failed\n", n);
            goto cleanup_fail;
         }
         curBuff = (curBuff + 1) % kBufCnt;
      }

      /* ---- Subtest 1: toggle_disable (quiesce-and-reset) ----
       *
       * The sequential setWriteEnable(0) + setReadEnable(0) pair is NOT atomic
       * vs the 1ms emu_gpu_poll kthread: in the us-scale window between the
       * two BAR0 writes the kthread can fire and process up to kBufCnt pending
       * ticks on the direction whose enable has not yet been cleared. Prior
       * implementation used an instantaneous rxN0==rxN1 && txN0==txN1 snapshot
       * pair which failed as `rx 100->100 tx 98->99`.
       *
       * Fix: after the disable-pair, wait 100ms for the kthread to quiesce,
       * reset counters via BAR0 CntRst to establish a known-zero baseline,
       * then wait another 100ms and assert both counters remain zero. This
       * is race-immune because CntRst happens after the kthread has already
       * drained any pending in-flight ticks, and any further tick after
       * CntRst would require re-enabling — which this subtest does not do. */
      regs.setWriteEnable(0);
      regs.setReadEnable(0);
      ::usleep(100000);               /* drain pending kthread tick */
      regs.countReset();              /* zero baseline for both counters */
      ::usleep(100000);               /* verify no motion post-reset */
      uint32_t rxQ = regs.rxFrameCount();
      uint32_t txQ = regs.txFrameCount();
      {
         char buf[96];
         snprintf(buf, sizeof(buf),
            "(post-reset rx=%u tx=%u expected=0)", rxQ, txQ);
         report("toggle_disable", (rxQ == 0) && (txQ == 0), buf);
      }

      /* ---- Subtest 2: toggle_resume ---- */
      /* Re-enable engine; arm doorbells so round-robin starts from buffer 0.
       * Observable contract: VHDL-faithful nextWriteIdx resets to
       * 0 on disable, so the engine resumes from buffer 0 post-enable. */
      regs.setWriteEnable(1);
      regs.setReadEnable(1);

      /* Re-arm all doorbells for the resumed run (freelist + rx doorbell clear). */
      for (int i = 0; i < kBufCnt; ++i) {
         regs.writeReg(regs.freeListOffset(i), 1);
         *reinterpret_cast<volatile uint32_t *>(rxBuffs[i] + 4) = 0;
         *reinterpret_cast<volatile uint32_t *>(txBuffs[i])     = 1;
      }

      int frameErrors = 0;
      curBuff = 0;
      for (int n = 0; n < kResumeFrames; ++n) {
         /* Verify every resume frame so userspace PrbsData._sequence
          * tracks the kernel's rx_prbs_seq byte-for-byte. Without this, the
          * subsequent maxbuffers pre-reduce loop's verify=true frame 0
          * compares kernel state at seq=N against stale userspace state at
          * seq=N-kResumeFrames+1 and fails with "PRBS mismatch size=65520
          * buf=0". The continuity check remains correct (runOneFrame only
          * checks when verify=true), just extended from "first frame" to
          * "every frame" for state-sync discipline. */
         if (runOneFrame(regs, kBufCnt, curBuff, rxBuffs, txBuffs, kBufSize,
                         &prbs, /*verify=*/true) != 0) {
            ++frameErrors;
            break;
         }
         curBuff = (curBuff + 1) % kBufCnt;
      }
      report("toggle_resume", frameErrors == 0, "");

      /* ---- Subtest 3: maxbuffers_4to2 ---- */
      /* Snapshot the filtered-dmesg line count before the subtest begins so
       * the post-run scan only inspects lines emitted during this subtest.
       * Unscoped `tail -50` over the whole ring buffer would catch boot-time
       * or prior-test WARNs as false positives. Also record whether
       * dmesg returned any output at all; on kernels with
       * kernel.dmesg_restrict=1 an unprivileged reader gets an empty stream
       * and we must not claim PASS vacuously. */
      unsigned long dmesg_base_lines = 0;
      bool dmesg_available = false;
      {
         FILE *fp0 = popen(
            "dmesg --ctime --level=emerg,alert,crit,err,warn 2>/dev/null"
            " | wc -l", "r");
         if (fp0 != nullptr) {
            if (fscanf(fp0, "%lu", &dmesg_base_lines) != 1) {
               dmesg_base_lines = 0;
            }
            int rc0 = pclose(fp0);
            /* wc -l prints 0 even for an empty (restricted) stream, so
             * distinguish by requiring a clean pclose AND non-zero baseline.
             * A zero baseline on a long-lived CI kernel is implausible and
             * strongly suggests dmesg is locked out. */
            dmesg_available = (rc0 == 0 && dmesg_base_lines > 0);
         }
      }

      /* Run half of kPreambleFrames more at kBufCnt=4 to re-establish traffic.
       * verify=true keeps PrbsData._sequence in lockstep with kernel
       * across the maxbuffers reduction (kernel advances every frame). */
      for (int n = 0; n < kPreambleFrames / 2; ++n) {
         if (runOneFrame(regs, kBufCnt, curBuff, rxBuffs, txBuffs, kBufSize,
                         &prbs, /*verify=*/true) != 0) {
            fprintf(stderr,
               "dmaGpuToggleTest: maxbuffers pre-reduce frame %d failed\n", n);
            goto cleanup_fail;
         }
         curBuff = (curBuff + 1) % kBufCnt;
      }

      /* Mid-flight buffer count reduction: 4->2 via BAR0 writes.
       * Kernel reads writeCount once per tick and clamps nextWriteIdx via
       * idx % (wcnt + 1) (gpu_engine.c:218).
       * N = activeBufs - 1, so setWriteCount(1) = 2 active buffers. */
      regs.setWriteCount(1);
      regs.setReadCount(1);
      int reducedBufCnt = 2;

      /* Run kPostReduceFrames frames with bufCnt=2 (curBuff wraps 0,1,0,1,...).
       * verify=true keeps PrbsData._sequence aligned (kernel advances
       * regardless of bufCnt, so userspace must verify every frame). */
      for (int n = 0; n < kPostReduceFrames; ++n) {
         if (runOneFrame(regs, reducedBufCnt, curBuff % reducedBufCnt,
                         rxBuffs, txBuffs, kBufSize,
                         &prbs, /*verify=*/true) != 0) {
            fprintf(stderr,
               "dmaGpuToggleTest: maxbuffers post-reduce frame %d failed\n", n);
            goto cleanup_fail;
         }
         curBuff = (curBuff + 1) % reducedBufCnt;
      }

      /* Check dmesg for WARN/BUG/KASAN attributable to the maxbuffers subtest.
       * Scope the scan to lines produced *after* dmesg_base_lines (captured
       * before the subtest started). Using `tail -n +<base+1>` over the same
       * filtered stream avoids catching unrelated WARNs from boot or prior
       * tests.
       *
       * If the baseline snapshot already showed dmesg is unavailable
       * (kernel.dmesg_restrict=1 plus unprivileged euid returns an empty
       * stream), or pclose reports failure, we cannot claim PASS — a vacuous
       * "zero matches" would hide a real WARN/BUG the run actually emitted.
       * In that case, report the subtest as inconclusive (not PASS) and
       * defer to scripts/ci/check-dmesg.sh which runs with CAP_SYSLOG. */
      bool dmesgClean = true;
      bool dmesg_scan_ok = false;
      {
         char cmd[192];
         snprintf(cmd, sizeof(cmd),
            "dmesg --ctime --level=emerg,alert,crit,err,warn 2>/dev/null"
            " | tail -n +%lu", dmesg_base_lines + 1UL);
         FILE *fp = popen(cmd, "r");
         if (fp != nullptr) {
            char line[512];
            while (fgets(line, sizeof(line), fp) != nullptr) {
               /* Match the kernel's own canonical tokens rather than loose
                * substrings. Bare strstr(line,"BUG") would match the common
                * "DEBUG" logging level (pr_debug, dev_dbg, SLAC dbg: prints)
                * and trip false positives on otherwise-clean runs. */
               if (strstr(line, "------------[ cut here ]------------") != nullptr ||
                   strstr(line, "Call Trace:") != nullptr ||
                   strstr(line, "WARNING: CPU:") != nullptr ||
                   strstr(line, "BUG: ") != nullptr ||  /* trailing space excludes "DEBUG" */
                   strstr(line, "KASAN:") != nullptr) {
                  fprintf(stderr,
                     "dmaGpuToggleTest: dmesg flag: %s", line);
                  dmesgClean = false;
               }
            }
            int rc = pclose(fp);
            dmesg_scan_ok = (rc == 0);
            if (rc != 0) {
               fprintf(stderr,
                  "dmaGpuToggleTest: dmesg scan failed (pclose rc=%d)\n", rc);
            }
         }
      }
      if (!dmesg_available || !dmesg_scan_ok) {
         fprintf(stderr,
            "dmaGpuToggleTest: dmesg unavailable (dmesg_restrict=1 or EOF);"
            " deferring to scripts/ci/check-dmesg.sh — not claiming PASS.\n");
         dmesgClean = false;
         report("maxbuffers_4to2", false, "(dmesg inconclusive)");
      } else {
         report("maxbuffers_4to2", dmesgClean, dmesgClean ? "" : "(dmesg WARN/BUG/KASAN)");
      }
   }

   /* Teardown. Disarm atexit first so the normal close sequence below is
    * the single source of truth for fd lifetime; the atexit handler becomes
    * a safety net only used on std::exit(1) paths from runOneFrame. */
   s_atexit_armed = false;
   gpuRemNvidiaMemory(fd);
   for (int i = 0; i < kBufCnt; ++i) {
      gpuEmuFreeBuf(rxBuffs[i], kBufSize);
      gpuEmuFreeBuf(txBuffs[i], kBufSize);
   }
   ::close(stub_fd);
   dmaUnMapRegister(fd, bar0, GPU_ASYNC_CORE_SIZE);
   ::close(fd);

   printf("=== Summary: %d passed, %d failed ===\n", gPassed, gErrors);
   return gErrors > 0 ? 1 : 0;

cleanup_fail:
   /* Same invariant as happy-path teardown: disarm atexit before the
    * explicit close sequence so the handler does not double-close. */
   s_atexit_armed = false;
   gpuRemNvidiaMemory(fd);
   for (int i = 0; i < rxAllocCnt; ++i) gpuEmuFreeBuf(rxBuffs[i], kBufSize);
   for (int i = 0; i < txAllocCnt; ++i) gpuEmuFreeBuf(txBuffs[i], kBufSize);
   ::close(stub_fd);
   dmaUnMapRegister(fd, bar0, GPU_ASYNC_CORE_SIZE);
   ::close(fd);
   return 1;
}
