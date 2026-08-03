/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    RDMA loopback test against the FPGA-less emulator.
 *
 *    A CUDA-free C++17 port of rdmaTest.cu::runSimpleLoop with three
 *    cross-language substitutions:
 *       cudaMalloc                -> gpuEmuAllocBuf (stub miscdevice + mmap)
 *       cudaMemcpyAsync           -> std::memcpy
 *       cuStreamWaitValue32 GEQ 1 -> std::atomic<uint32_t>::load(acquire)
 *                                    + __builtin_ia32_pause + 10s deadline
 *
 *    Closes the FPGA -> GPU -> FPGA path byte-exactly through the
 *    datadev_emulator + nvidia_p2p_stub stack with no real GPU and no
 *    CUDA toolkit. The single-size, non-sweep path covers basic
 *    loopback; --sweep layers the payload matrix on top.
 *
 *    Expected environment:
 *       - datadev_emulator.ko loaded (with emu_gpu_max_buffers param set)
 *       - nvidia_p2p_stub.ko loaded (provides nvidia_p2p_* + addr table)
 *       - datadev.ko built with NVIDIA_DRIVERS=$(pwd)/emulator/gpu_stub,
 *         loaded against the emulator
 *       - /dev/datadev_0 present, BAR0 GpuAsync version >= 4 (v5 emulated)
 *       - /dev/nvidia_p2p_stub_mem present
 *
 *    Exit code: 0 on full success; 1 on any failure (ioctl/mmap error,
 *    version mismatch, PRBS mismatch, timeout, counter mismatch,
 *    hdr.size bound violation).
 *-----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the aes_stream_drivers package, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 *-----------------------------------------------------------------------------
**/

/* System headers */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <string>

/* Project headers. */
#include <PrbsData.h>
#include <DmaDriver.h>      /* dmaMapRegister / dmaUnMapRegister */
#include <AxisDriver.h>     /* pulls DmaDriver.h again, harmless */
#include <GpuAsyncRegs.h>   /* GPU_ASYNC_CORE_OFFSET + size, V4 reg defs */
#include <GpuAsyncUser.h>   /* GpuAsyncCoreRegs wrapper */
#include <GpuAsync.h>       /* gpuAddNvidiaMemory + version helpers */
#include "emu_gpu_addr_table.h" /* STUB_RESERVE_BUF uapi (via -I gpu_stub/src) */

/* ---------------------------------------------------------------------------
 * Constants + types
 * -------------------------------------------------------------------------*/

static constexpr char const *kDefaultDev = "/dev/datadev_0";
static constexpr char const *kStubDev    = "/dev/nvidia_p2p_stub_mem";
static constexpr uint32_t    kDefaultSize             = 65536;
static constexpr int         kDefaultCount            = 100;
static constexpr uint32_t    kRequiredGpuAsyncVersion = 4;
static constexpr int         kMaxBufCnt               = 1024;  /* matches kernel */
static constexpr int         kPollDeadlineSeconds     = 10;

/*
 * Hard-coded payload sweep matrix. No env var, no flag override. If a
 * caller wants a different single size, use -s without --sweep. The 1 MB
 * entry exercises alloc_pages(order=8) on the kernel side.
 *
 * Every entry must be a multiple of 64 KB: the datadev Gpu_AddNvidia
 * ioctl at gpu_async.c:162 rejects sizes where (size & 0xFFFF) != 0
 * with -EINVAL. Matrix spans 64 KB -> 1 MB to still exercise order=8.
 */
static constexpr uint32_t    kSweepSizes[]   = { 65536, 131072, 524288, 1048576 };
static constexpr size_t      kSweepSizeCount =
   sizeof(kSweepSizes) / sizeof(kSweepSizes[0]);

/*
 * AxiWrDesc64_t at the start of every RX buffer. 128-bit (16 byte) layout
 * per project_desc128_deprecated: header at +0, size at +4, 8 bytes of pad
 * so payload starts at +DATA_BYTES_C (16).
 *
 * NOTE: the existing rdmaTest.cu defines this as 8 bytes (helper accessors
 * only). The 128-bit Desc128En=1 layout this binary targets is the only
 * mode the emulator generates frames in, so the literal 16-byte layout is
 * what the kernel writes and what we need to walk past to reach the
 * payload bytes the PrbsData::processData verify consumes.
 */
typedef struct {
   uint32_t header;
   uint32_t size;
   uint8_t  pad[8];
} AxiWrDesc64_t;
static_assert(sizeof(AxiWrDesc64_t) == 16, "AxiWrDesc64_t must be 16 bytes");

struct Args {
   std::string dev    = kDefaultDev;
   int         bufCnt = 0;            /* 0 = auto-detect via gpuGetMaxBuffers */
   uint32_t    size   = kDefaultSize;
   int         count  = kDefaultCount;
   int         verbose = 0;
   bool        sweep   = false;       /* --sweep long-only flag */
};

static int s_verbose = 0;

/* Cleanup state. main() arms these via atexit and clears them on
 * the happy-path teardown so the handler is idempotent. The handler
 * runs on every exit path including std::exit(1) from runSimpleLoop /
 * runSweep / reallocBuffersAtSize / the deadline-spin helper. */
static int  s_atexit_fd      = -1;
static int  s_atexit_stub_fd = -1;
static bool s_atexit_armed   = false;

static void rdma_test_atexit(void) {
   if (!s_atexit_armed) return;
   s_atexit_armed = false;          /* idempotent: only run once */
   if (s_atexit_fd >= 0) {
      /* Best-effort unregister; ignore errors (kernel may have already
       * dropped state if module was unloaded). */
      (void)gpuRemNvidiaMemory(s_atexit_fd);
      ::close(s_atexit_fd);
      s_atexit_fd = -1;
   }
   if (s_atexit_stub_fd >= 0) {
      ::close(s_atexit_stub_fd);
      s_atexit_stub_fd = -1;
   }
}

/* ---------------------------------------------------------------------------
 * CLI
 * -------------------------------------------------------------------------*/

static void show_help(const char *prog) {
   fprintf(stderr,
      "Usage: %s [-d /dev/datadev_0] [-b N] [-s bytes] [-c frames] [-v] [--sweep]\n"
      "  -d <path>   datadev path (default %s)\n"
      "  -b <N>      buffer count (default: gpuGetMaxBuffers(fd))\n"
      "  -s <bytes>  frame size (default %u)\n"
      "  -c <N>      frame count (default %d)\n"
      "  -v          verbose (repeatable)\n"
      "  --sweep     iterate the payload matrix {65536, 131072, 524288, 1048576}\n"
      "              running -c frames per size; first failure aborts\n",
      prog, kDefaultDev, kDefaultSize, kDefaultCount);
}

static void parse_args(int argc, char **argv, Args &a) {
   static const struct option long_opts[] = {
      {"sweep", no_argument, nullptr, 1000},
      {nullptr, 0,           nullptr, 0},
   };
   int opt;
   while ((opt = getopt_long(argc, argv, "d:b:s:c:vh",
                              long_opts, nullptr)) != -1) {
      switch (opt) {
         case 'd':  a.dev = optarg; break;
         case 'b':  a.bufCnt = std::atoi(optarg); break;
         case 's':  a.size   = static_cast<uint32_t>(std::strtoul(optarg, nullptr, 0)); break;
         case 'c':  a.count  = std::atoi(optarg); break;
         case 'v':  a.verbose++; break;
         case 1000: a.sweep = true; break;       /* --sweep long-only */
         case 'h':  show_help(argv[0]); std::exit(0);
         default:   show_help(argv[0]); std::exit(1);
      }
   }
   /* Reject invalid -s / -c so a typo (-s abc -> 0) doesn't silently
    * produce a no-op run that reports PASS. -b is range-checked later
    * against gpuGetMaxBuffers/kMaxBufCnt; -s upper bound is enforced by
    * the kernel cfgSize on the first dmaWrite. */
   if (a.size == 0) {
      fprintf(stderr, "rdmaTestEmu: invalid -s size (must be > 0)\n");
      std::exit(1);
   }
   if (a.count <= 0) {
      fprintf(stderr, "rdmaTestEmu: invalid -c count (must be > 0)\n");
      std::exit(1);
   }
   s_verbose = a.verbose;
}

/* ---------------------------------------------------------------------------
 * Buffer helpers
 *
 * gpuEmuAllocBuf reserves a stub buffer via STUB_RESERVE_BUF and mmaps it.
 * The stub's mmap converts vma->vm_pgoff back to buf_id, so we encode
 * buf_id in the offset as buf_id * page_size (sysconf gives the same
 * PAGE_SIZE the stub uses for vm_pgoff arithmetic).
 *
 * gpuEmuFreeBuf is plain munmap. The stub's FD-holder refcount is dropped
 * on close(stub_fd), not per-mmap.
 * -------------------------------------------------------------------------*/

static void *gpuEmuAllocBuf(int stub_fd, size_t size, uint32_t *out_buf_id) {
   struct stub_reserve_req req;
   std::memset(&req, 0, sizeof(req));
   req.size = static_cast<uint32_t>(size);

   if (::ioctl(stub_fd, STUB_RESERVE_BUF, &req) < 0) {
      fprintf(stderr,
         "rdmaTestEmu: STUB_RESERVE_BUF failed size=%zu: %s\n",
         size, std::strerror(errno));
      return nullptr;
   }

   long page_size = ::sysconf(_SC_PAGESIZE);
   if (page_size <= 0) page_size = 4096;
   off_t offset = static_cast<off_t>(req.buf_id) * page_size;

   /* The datadev driver derives virt_offset = (user_va & 0xFFFF) from the
    * VA we register via gpuAddNvidiaMemory and writes fake_dma + virt_offset
    * into BAR0 (gpu_async.c:230). The stub registers addr_table entries
    * only at fake_dma + i*64KB boundaries (nvidia_p2p_stub.c:175). A VA
    * that isn't 64KB-aligned yields a non-zero virt_offset, so the
    * emulator's addr_lookup misses on every tick. Force 64KB alignment
    * by reserving size+64KB anonymously, picking the aligned sub-range,
    * and replacing it with a MAP_FIXED mapping of the stub buffer. */
   constexpr size_t kAlign = 64 * 1024;
   void *rsv = ::mmap(nullptr, size + kAlign, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   if (rsv == MAP_FAILED) {
      fprintf(stderr,
         "rdmaTestEmu: mmap reservation size=%zu failed: %s\n",
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
         "rdmaTestEmu: mmap(buf_id=%u size=%zu) failed: %s\n",
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
   /* No ioctl; the stub drops the FD-holder refcount on close(stub_fd). */
}

/* ---------------------------------------------------------------------------
 * Acquire-load + PAUSE spin with deadline
 *
 * Polls a kernel-written 32-bit doorbell with std::memory_order_acquire
 * to pair with the emulator RX/TX tick's smp_wmb() at gpu_engine.c:266.
 * Deadline checked every 1024 PAUSEs (~5-10 us at 5-10 ns per PAUSE on
 * x86) to avoid syscalling per iteration; worst-case overshoot is
 * microsecond-scale against a 10 second budget.
 *
 * On timeout: print one of
 *   "rdmaTestEmu: rx doorbell timeout buf=N elapsed=X.Xs"
 *   "rdmaTestEmu: tx ack timeout buf=N elapsed=X.Xs"
 * and exit 1. Both forms are emitted via the same printf below using
 * a %s tag substitution; the literal phrases live in the call sites
 * (see runSimpleLoop's emu_wait_value32_with_deadline calls).
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
               "rdmaTestEmu: %s timeout buf=%u elapsed=%.1fs\n",
               tag, buf, secs);
            std::exit(1);
         }
      }
   }
}

/* ---------------------------------------------------------------------------
 * runSimpleLoop port
 *
 * Faithful port of rdmaTest.cu's runSimpleLoop with the three
 * substitutions for the CPU emulator. Required sequencing:
 * gpuEnableTx(0)/gpuEnableRx(0), arm free-list + clear doorbells
 * per buffer, setRemoteWriteMaxSize, then enables go LAST. The
 * rxBuffs[i]+4 pre-clear in the arming block plus the per-frame clear
 * after the memcpy prevents a stale doorbell from acking the next frame.
 * -------------------------------------------------------------------------*/

static void runSimpleLoop(GpuAsyncCoreRegs &regs, int fd, int bufCnt,
                          uint8_t **rxBuffs, uint8_t **txBuffs,
                          int frameCount, uint32_t bufSize) {
   const uint32_t dmaHeaderSize = regs.dmaDataBytes();

   /* Underflow guard: `bufSize - dmaHeaderSize` is used on the next
    * setRemoteWriteMaxSize write AND as the hdr.size ceiling inside the
    * per-frame bound-check. If `bufSize <= dmaHeaderSize`, the subtraction
    * wraps as uint32_t, both the register write and the bound-check
    * trivially succeed for any hdr.size, and the payload memcpy below
    * runs OOB on txBuffs[i]/rxBuffs[i] (allocated only `bufSize` bytes).
    * Reject at runtime — dmaHeaderSize is a BAR-sourced runtime value,
    * so this can't be folded into the CLI-parse check on a.size. */
   if (bufSize <= dmaHeaderSize) {
      fprintf(stderr,
         "rdmaTestEmu: bufSize=%u must exceed dmaHeaderSize=%u "
         "(raise -s <size>)\n",
         bufSize, dmaHeaderSize);
      std::exit(1);
   }

   /* Stop both directions before re-arming any buffer.
    *
    * Apply the quiesce-and-reset pattern: the two disable writes are
    * sequential and the 1ms kthread can fire between them, bumping a
    * counter after the first disable but before the second. 100ms settle
    * -> countReset -> 100ms settle makes the zero baseline race-immune
    * regardless of which direction was still enabled when this function
    * was entered (sweep mode enters with enables already 0 from
    * reallocBuffersAtSize; single-size mode enters fresh with enables
    * effectively 0 at post-insmod default -- both are safe). Isolates
    * this run's counters from any prior iteration (sweep mode calls
    * runSimpleLoop multiple times; without reset, later iterations
    * inherit earlier frame counts). */
   gpuEnableTx(fd, 0);
   gpuEnableRx(fd, 0);
   ::usleep(100000);       /* drain pending kthread tick */
   regs.countReset();      /* isolate this run's counters */
   ::usleep(100000);       /* confirm no motion post-reset */

   /* Buffer count is established by gpuAddNvidiaMemory during registration;
    * no explicit setWriteCount/setReadCount needed here. */

   for (int i = 0; i < bufCnt; ++i) {
      /* Arm FPGA's free-list slot for buffer i. */
      regs.writeReg(regs.freeListOffset(i), 1);

      /* Clear the GPU's doorbell at rxBuffs[i]+4 so a stale doorbell
       * does not spuriously ack the first frame of this run. */
      *reinterpret_cast<volatile uint32_t *>(rxBuffs[i] + 4) = 0;

      /* Arm the GPU-side free-list at txBuffs[i]+0. The emulator TX tick
       * looks at this slot and treats 1 as "GPU has buffer ready"; we
       * clear it back to 0 after the per-frame memcpy completes. */
      *reinterpret_cast<volatile uint32_t *>(txBuffs[i]) = 1;
   }

   /* Set max FPGA->GPU write size before re-enabling direction bits.
    * Per rdmaTest.cu convention: RemoteWriteMaxSize holds the PAYLOAD
    * size (what the emulator writes as hdr.size and the number of PRBS
    * bytes it fills at kva + dmaHeaderSize). Buffer total = payload +
    * dmaHeaderSize. bufSize is the 64KB-multiple buffer allocation,
    * so payload = bufSize - dmaHeaderSize. */
   regs.setRemoteWriteMaxSize(0, bufSize - dmaHeaderSize);

   /* Now bring both directions back online. */
   gpuEnableTx(fd, 1);
   gpuEnableRx(fd, 1);

   int curBuff = 0;
   for (int n = 0; n < frameCount; ++n) {
      AxiWrDesc64_t hdr;

      /* (1) wait for FPGA's RX doorbell on buffer curBuff (10s deadline). */
      emu_wait_value32_with_deadline(
         reinterpret_cast<uint32_t *>(rxBuffs[curBuff] + 4), 1,
         std::chrono::seconds(kPollDeadlineSeconds),
         "rx doorbell", static_cast<uint32_t>(curBuff));

      /* (2) read header. Plain memcpy is sufficient; the acquire load
       * above already synchronizes-with the emulator RX tick's smp_wmb,
       * so the entire 16-byte header has happens-before publication.   */
      std::memcpy(&hdr, rxBuffs[curBuff], sizeof(hdr));

      /* Bound-check hdr.size before the memcpy. The kernel side already
       * clamps at gpu_engine.c:245-246, but defense-in-depth keeps a
       * corrupted/spoofed doorbell from turning into an OOB memcpy on
       * the userspace side. */
      if (hdr.size > bufSize - dmaHeaderSize) {
         fprintf(stderr,
            "rdmaTestEmu: hdr.size=%u exceeds buffer-capacity=%u\n",
            hdr.size, bufSize - dmaHeaderSize);
         std::exit(1);
      }

      /* First-frame PRBS verify. PrbsData(32, 4, 1, 2, 6, 31) matches
       * the same LFSR taps the kernel-side prbs.c uses to generate the
       * payload. Only verify the first frame; per-frame verify is
       * unnecessary -- the emulator's TX-side PRBS verify already covers
       * every frame. */
      if (n == 0) {
         PrbsData prbs(32, 4, 1, 2, 6, 31);
         if (!prbs.processData(rxBuffs[curBuff] + dmaHeaderSize, hdr.size)) {
            fprintf(stderr, "rdmaTestEmu: PRBS mismatch size=%u buf=%d\n",
                    hdr.size, curBuff);
            std::exit(1);
         }
      }

      /* (3) wait for emulator TX-side ack at txBuffs[curBuff] (10s). */
      emu_wait_value32_with_deadline(
         reinterpret_cast<uint32_t *>(txBuffs[curBuff]), 1,
         std::chrono::seconds(kPollDeadlineSeconds),
         "tx ack", static_cast<uint32_t>(curBuff));

      /* (4) memcpy the payload from the RX buffer to the TX buffer.
       * cudaMemcpyDeviceToDevice -> std::memcpy on the same physical
       * pages (the reuse path makes user VA + emulator kva alias the
       * same alloc_pages run). */
      std::memcpy(txBuffs[curBuff] + dmaHeaderSize,
                  rxBuffs[curBuff] + dmaHeaderSize, hdr.size);

      /* (5) clear GPU-side TX free-list slot — emulator TX tick gates
       * the next round trip on this back to 1 before re-arming. */
      *reinterpret_cast<volatile uint32_t *>(txBuffs[curBuff]) = 0;

      /* (6) write remoteReadSize -> emulator's "FPGA doorbell" that
       * tells the TX tick how many bytes to consume from txBuffs. */
      regs.writeReg(regs.remoteReadSizeOffset(curBuff), hdr.size);

      /* (7) clear GPU-side RX doorbell — emulator's RX tick gates the
       * next FPGA->GPU write on this back to 0. */
      *reinterpret_cast<volatile uint32_t *>(rxBuffs[curBuff] + 4) = 0;

      /* (8) re-arm the FPGA's free-list slot for this buffer. */
      regs.writeReg(regs.freeListOffset(curBuff), 1);

      if (s_verbose > 1) {
         printf("frame=%d buf=%d size=%u\n", n, curBuff, hdr.size);
      }

      /* (9) round-robin to the next buffer. */
      if (++curBuff >= bufCnt) curBuff = 0;
   }

   /* Step (8) re-arms `freeList[curBuff]=1` on every frame including
    * the last, so at loop exit all bufCnt slots are armed. The 1ms
    * emu_gpu_poll kthread will drain them via rx_tick (each drain
    * bumps rxFrameCnt by 1) before the 2-second counter-convergence
    * poll below can snapshot the expected value. Without this drain,
    * RX leaks by exactly bufCnt (e.g. rx=104 tx=100 for bufCnt=4).
    *
    * Fix: clear every free-list slot, then settle 100ms so any tick
    * already in-flight when we cleared can complete its bump. The
    * race is benign because rx_tick re-reads freeList inside its
    * spinlock-protected region (gpu_engine.c:278-281): if the kthread
    * sees 0 it skips; if it already committed to processing, the bump
    * lands during the settle and is absorbed by the convergence poll
    * exiting cleanly with rx == expected. */
   for (int i = 0; i < bufCnt; ++i) {
      regs.writeReg(regs.freeListOffset(i), 0);
   }
   ::usleep(100000);   /* let any in-flight tick complete its bump */

   /* Final counter equality check. The emulator bumps RxFrameCnt / TxFrameCnt AFTER writing the user-visible doorbell /
    * ack (gpu_engine.c:268-300 for RX; 381-402 for TX); the last
    * frame's counter bump can lag the loop exit by a few ticks. Poll
    * briefly for convergence before declaring a mismatch. */
   uint32_t rxN = 0, txN = 0;
   const uint32_t expected = static_cast<uint32_t>(frameCount);
   auto deadline = std::chrono::steady_clock::now() +
                   std::chrono::seconds(2);
   do {
      rxN = regs.rxFrameCount();
      txN = regs.txFrameCount();
      if (rxN == expected && txN == expected) break;
      __builtin_ia32_pause();
   } while (std::chrono::steady_clock::now() < deadline);
   if (rxN != expected || txN != expected) {
      fprintf(stderr,
         "rdmaTestEmu: counter mismatch rx=%u tx=%u expected=%d\n",
         rxN, txN, frameCount);
      std::exit(1);
   }

   if (s_verbose > 0) {
      printf("rdmaTestEmu: %d frames OK (size=%u, bufCnt=%d)\n",
             frameCount, bufSize, bufCnt);
   }
}

/* ---------------------------------------------------------------------------
 * reallocBuffersAtSize (canonical pause-realloc-resume sequence)
 *
 * Between sweep sizes: pause the engine, drop driver + FD holders,
 * allocate fresh buffers at the new size, re-register, reconfigure BAR0
 * counts. runSimpleLoop below re-arms doorbells and flips the enables
 * back on, so no separate resume here.
 *
 * On any sub-step failure this helper exits(1) with a specific
 * diagnostic (consistent with the file-wide error-path idiom).
 * -------------------------------------------------------------------------*/

static void reallocBuffersAtSize(
      GpuAsyncCoreRegs &regs, int fd, int stub_fd, int bufCnt,
      uint8_t **rxBuffs, uint32_t *rxBufIds,
      uint8_t **txBuffs, uint32_t *txBufIds,
      uint32_t newSize, uint32_t oldSize) {
   /* Same underflow guard as runSimpleLoop. Step 6 below writes
    * setRemoteWriteMaxSize(newSize - dmaDataBytes()), which wraps if
    * newSize <= dmaDataBytes(). kSweepSizes[] is currently all >= 65536
    * so this is defense-in-depth, not a live bug — but a future sweep
    * table change that added a smaller entry would turn into a silent
    * OOB on the next runSimpleLoop iteration. */
   if (newSize <= regs.dmaDataBytes()) {
      fprintf(stderr,
         "rdmaTestEmu: reallocBuffersAtSize newSize=%u must exceed "
         "dmaDataBytes=%u\n",
         newSize, regs.dmaDataBytes());
      std::exit(1);
   }

   /* 1. Pause the poll thread BEFORE touching any registration.
    *
    * The sequential gpuEnableTx(0) + gpuEnableRx(0) pair is NOT atomic
    * vs the 1ms emu_gpu_poll kthread. Between the two BAR0 writes the
    * kthread can fire and process up to kBufCnt pending ticks on whichever
    * direction still has its enable bit set. Without the quiesce-and-reset
    * below the symptom is a stale counter bump that leaks into the next
    * per-size iteration (rx=104 tx=100 expected=100).
    *
    * Fix: after the disable-pair, wait 100ms for the kthread to drain,
    * countReset() to zero the BAR0 counters for a fresh baseline, then
    * wait 100ms more before re-enabling. */
   gpuEnableTx(fd, 0);
   gpuEnableRx(fd, 0);
   ::usleep(100000);       /* drain pending kthread tick */
   regs.countReset();      /* zero baseline — eliminates race-window leak */
   ::usleep(100000);       /* confirm no motion post-reset */

   /* 2. Driver-side unregister. Zeros BAR0 writeAddr/readAddr slots
    *    and drops driver-holder refcount for every buffer. */
   if (gpuRemNvidiaMemory(fd) < 0) {
      fprintf(stderr,
         "rdmaTestEmu: gpuRemNvidiaMemory failed: %s\n",
         std::strerror(errno));
      std::exit(1);
   }

   /* 3. Drop FD-holder refcount on each old buffer (munmap). */
   for (int i = 0; i < bufCnt; ++i) {
      gpuEmuFreeBuf(rxBuffs[i], oldSize);
      gpuEmuFreeBuf(txBuffs[i], oldSize);
      rxBuffs[i] = nullptr;
      txBuffs[i] = nullptr;
   }

   /* 4. Allocate fresh buffers at the new size. The stub backs each
    *    allocation with __GFP_ZERO so the stale-doorbell invariant holds. */
   for (int i = 0; i < bufCnt; ++i) {
      rxBuffs[i] = static_cast<uint8_t *>(
         gpuEmuAllocBuf(stub_fd, newSize, &rxBufIds[i]));
      if (!rxBuffs[i]) {
         fprintf(stderr,
            "rdmaTestEmu: rx alloc failed at size=%u buf=%d\n",
            newSize, i);
         std::exit(1);
      }
      txBuffs[i] = static_cast<uint8_t *>(
         gpuEmuAllocBuf(stub_fd, newSize, &txBufIds[i]));
      if (!txBuffs[i]) {
         fprintf(stderr,
            "rdmaTestEmu: tx alloc failed at size=%u buf=%d\n",
            newSize, i);
         std::exit(1);
      }
   }

   /* 5. Re-register with the driver. The nvidia_p2p_get_pages VMA-reuse
    *    path ensures user-VA and emulator-kva share the same physical
    *    pages. */
   for (int i = 0; i < bufCnt; ++i) {
      if (gpuAddNvidiaMemory(fd, 1,
            reinterpret_cast<uint64_t>(rxBuffs[i]), newSize) < 0) {
         fprintf(stderr,
            "rdmaTestEmu: gpuAddNvidiaMemory(write=1, buf=%d, size=%u) failed: %s\n",
            i, newSize, std::strerror(errno));
         std::exit(1);
      }
      if (gpuAddNvidiaMemory(fd, 0,
            reinterpret_cast<uint64_t>(txBuffs[i]), newSize) < 0) {
         fprintf(stderr,
            "rdmaTestEmu: gpuAddNvidiaMemory(write=0, buf=%d, size=%u) failed: %s\n",
            i, newSize, std::strerror(errno));
         std::exit(1);
      }
   }

   /* 6. Update header for the new size. The buffer count is re-established
    *    by the gpuAddNvidiaMemory calls above, so no explicit count write is
    *    needed. RemoteWriteMaxSize holds payload size, buffer = payload +
    *    dmaDataBytes(). */
   regs.setRemoteWriteMaxSize(0, newSize - regs.dmaDataBytes());

   /* runSimpleLoop re-arms free-list + doorbells and flips the enables
    * back on; no separate resume needed here. */
}

/* ---------------------------------------------------------------------------
 * runSweep
 *
 * Iterate kSweepSizes in order, running frameCount frames at each size.
 * First failure exits the process (inside runSimpleLoop or
 * reallocBuffersAtSize); subsequent sizes are not attempted.
 * -------------------------------------------------------------------------*/

static void runSweep(GpuAsyncCoreRegs &regs, int fd, int stub_fd,
                     int bufCnt,
                     uint8_t **rxBuffs, uint32_t *rxBufIds,
                     uint8_t **txBuffs, uint32_t *txBufIds,
                     int frameCount, uint32_t initialSize) {
   uint32_t currentSize = initialSize;
   for (size_t s = 0; s < kSweepSizeCount; ++s) {
      uint32_t newSize = kSweepSizes[s];
      if (s_verbose > 0) {
         fprintf(stdout, "rdmaTestEmu: sweep size=%u (%zu/%zu)\n",
                 newSize, s + 1, kSweepSizeCount);
      }

      /* Reallocate buffers at the new size unconditionally - simpler
       * than comparing against currentSize, and keeps the allocate/free
       * sequence canonical across sweep iterations. */
      reallocBuffersAtSize(regs, fd, stub_fd, bufCnt,
                           rxBuffs, rxBufIds,
                           txBuffs, txBufIds,
                           newSize, currentSize);
      currentSize = newSize;

      /* Run the full loop at this size. Internal failures (PRBS
       * mismatch, timeout, counter mismatch, hdr.size bound) call
       * std::exit(1) directly - no return-code plumbing needed. */
      runSimpleLoop(regs, fd, bufCnt, rxBuffs, txBuffs, frameCount, newSize);

      if (s_verbose > 0) {
         fprintf(stdout, "rdmaTestEmu: sweep size=%u PASSED\n", newSize);
      }
   }
}

/* ---------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/

int main(int argc, char **argv) {
   Args args;
   parse_args(argc, argv, args);

   /* Verbose announcement of mode (single-size vs --sweep matrix). */
   if (args.verbose > 0) {
      fprintf(stdout,
         "rdmaTestEmu: mode=%s count=%d size=%u\n",
         args.sweep ? "sweep" : "single", args.count, args.size);
   }

   /* Open datadev. */
   int fd = ::open(args.dev.c_str(), O_RDWR);
   if (fd < 0) {
      fprintf(stderr, "rdmaTestEmu: open(%s) failed: %s\n",
              args.dev.c_str(), std::strerror(errno));
      return 1;
   }

   /* Minimum-version gate (V4 register layout or newer; the emulator now
    * reports V5). gpuGetGpuAsyncVersion's return type is uint32_t for ABI
    * reasons (see include/GpuAsync.h), so a -1 ioctl error becomes
    * 0xFFFFFFFFu. A naive `(uint32_t)ver < 4` check would be vacuously false
    * and let the test proceed against a driver that never reported a
    * version. Cast to int32_t and reject negatives explicitly before the
    * version comparison. */
   int32_t ver = static_cast<int32_t>(gpuGetGpuAsyncVersion(fd));
   if (ver < 0) {
      fprintf(stderr,
         "rdmaTestEmu: GPU_Get_Gpu_Async_Ver ioctl failed (rc=%d, errno=%s)\n",
         ver, std::strerror(errno));
      ::close(fd);
      return 1;
   }
   if (static_cast<uint32_t>(ver) < kRequiredGpuAsyncVersion) {
      fprintf(stderr,
         "rdmaTestEmu: unsupported GPU Async version %d (v4+ required)\n", ver);
      ::close(fd);
      return 1;
   }

   /* Open the stub miscdevice for buffer reservations. */
   int stub_fd = ::open(kStubDev, O_RDWR);
   if (stub_fd < 0) {
      fprintf(stderr, "rdmaTestEmu: open(%s) failed: %s\n",
              kStubDev, std::strerror(errno));
      ::close(fd);
      return 1;
   }

   /* Arm atexit cleanup. Any std::exit(1) downstream (runSimpleLoop
    * counter mismatch, runSweep failure, reallocBuffersAtSize unregister
    * failure, deadline-spin timeout) now correctly drops the driver-side
    * GPU-memory mappings before terminating the process, preventing stale-
    * mapping EINVAL on the next test invocation. Assign FDs first, arm
    * the bool last, then register — so an early crash before arming cannot
    * run the handler with stale FD state. */
   s_atexit_fd      = fd;
   s_atexit_stub_fd = stub_fd;
   s_atexit_armed   = true;
   if (std::atexit(rdma_test_atexit) != 0) {
      fprintf(stderr, "rdmaTestEmu: std::atexit registration failed\n");
      /* Non-fatal — test can still run; cleanup just won't be guaranteed. */
      s_atexit_armed = false;
   }

   /* Determine buffer count: arg if positive, else BAR0 MaxBuffers. */
   int bufCnt = args.bufCnt;
   if (bufCnt <= 0) {
      bufCnt = static_cast<int>(gpuGetMaxBuffers(fd));
   }
   if (bufCnt <= 0 || bufCnt > kMaxBufCnt) {
      fprintf(stderr,
         "rdmaTestEmu: invalid bufCnt=%d (cap=%d)\n", bufCnt, kMaxBufCnt);
      ::close(stub_fd);
      ::close(fd);
      return 1;
   }

   /* Map BAR0 GPU async region. */
   void *bar0 = dmaMapRegister(fd, GPU_ASYNC_CORE_OFFSET, GPU_ASYNC_CORE_SIZE);
   if (!bar0) {
      fprintf(stderr, "rdmaTestEmu: dmaMapRegister failed\n");
      ::close(stub_fd);
      ::close(fd);
      return 1;
   }

   GpuAsyncCoreRegs regs(bar0);

   /* Defensive cleanup of stale BAR0 per-buffer state from a prior
    * test instance. test_gpu_dma_loopback.sh's run_with_retry harness
    * re-invokes this binary on a 10s tx-ack timeout flake, but the
    * prior instance's gpuRemNvidiaMemory only clears ctrl and
    * RW_MAX_SIZE — remoteReadSize[] and freeList[] survive. On the
    * next gpuAddNvidiaMemory(write=0) below, ctrl re-enables RE=1
    * with rcnt=0; the kernel TX tick immediately reads the stale
    * remoteReadSize[0]=non-zero, looks up the freshly-mapped (zero-
    * filled) TX buffer, and emu_prbs_process_data fires
    * WARN_ON_ONCE at prbs.c:123 because the new buffer's
    * data32[1]=0 yields event_length=4 != stale reqsz. Force a
    * clean slate here. ctrl was already cleared to WE=0/RE=0 by the
    * prior session's Gpu_RemNvidia, so no engine activity can race
    * these writes. */
   gpuEnableTx(fd, 0);
   gpuEnableRx(fd, 0);
   {
      const uint32_t maxBuffersHw = regs.maxBuffers();
      for (uint32_t i = 0; i < maxBuffersHw; ++i) {
         regs.setRemoteReadSize(i, 0);
         regs.writeReg(regs.freeListOffset(i), 0);
      }
   }

   /* Allocate parallel arrays for rx/tx buffers + their stub buf_ids. */
   uint8_t *rxBuffs[kMaxBufCnt] = {0};
   uint8_t *txBuffs[kMaxBufCnt] = {0};
   uint32_t rxBufIds[kMaxBufCnt] = {0};
   uint32_t txBufIds[kMaxBufCnt] = {0};
   int rxAllocCnt = 0, txAllocCnt = 0;
   /* Tracks the size the live buffers were last allocated with. After
    * --sweep this is kSweepSizes[last] (reallocBuffersAtSize replaced
    * them all); without --sweep it stays at args.size. Declared up here
    * so the goto-cleanup_fail jumps in the alloc loop don't cross its
    * initialization. */
   uint32_t liveSize = args.size;

   /* Reserve + mmap + register each rx and tx buffer. */
   for (int i = 0; i < bufCnt; ++i) {
      void *va = gpuEmuAllocBuf(stub_fd, args.size, &rxBufIds[i]);
      if (!va) goto cleanup_fail;
      rxBuffs[i] = static_cast<uint8_t *>(va);
      rxAllocCnt++;

      if (gpuAddNvidiaMemory(fd, 1,
            reinterpret_cast<uint64_t>(rxBuffs[i]), args.size) < 0) {
         fprintf(stderr,
            "rdmaTestEmu: gpuAddNvidiaMemory(write=1, buf=%d) failed: %s\n",
            i, std::strerror(errno));
         goto cleanup_fail;
      }

      va = gpuEmuAllocBuf(stub_fd, args.size, &txBufIds[i]);
      if (!va) goto cleanup_fail;
      txBuffs[i] = static_cast<uint8_t *>(va);
      txAllocCnt++;

      if (gpuAddNvidiaMemory(fd, 0,
            reinterpret_cast<uint64_t>(txBuffs[i]), args.size) < 0) {
         fprintf(stderr,
            "rdmaTestEmu: gpuAddNvidiaMemory(write=0, buf=%d) failed: %s\n",
            i, std::strerror(errno));
         goto cleanup_fail;
      }
   }

   /* Run the loopback. runSimpleLoop and runSweep both std::exit(1)
    * on any failure. After --sweep, the live buffer size is the LAST
    * entry of kSweepSizes (reallocBuffersAtSize replaced them all);
    * liveSize was initialized to args.size at the top of main. */
   if (args.sweep) {
      runSweep(regs, fd, stub_fd, bufCnt,
               rxBuffs, rxBufIds, txBuffs, txBufIds,
               args.count, args.size);
      liveSize = kSweepSizes[kSweepSizeCount - 1];
   } else {
      runSimpleLoop(regs, fd, bufCnt, rxBuffs, txBuffs, args.count, args.size);
   }

   /* Teardown: drop driver-side registrations, then unmap user VAs,
    * then close the stub FD (which drops FD-holder refcount on entries
    * via stub_release), then unmap BAR0, then close datadev.
    *
    * Disarm atexit before invoking the canonical teardown so the
    * registered handler does not double-free/double-close. */
   s_atexit_armed = false;
   gpuRemNvidiaMemory(fd);
   for (int i = 0; i < bufCnt; ++i) {
      gpuEmuFreeBuf(rxBuffs[i], liveSize);
      gpuEmuFreeBuf(txBuffs[i], liveSize);
   }
   ::close(stub_fd);
   dmaUnMapRegister(fd, bar0, GPU_ASYNC_CORE_SIZE);
   ::close(fd);

   return 0;

cleanup_fail:
   /* Best-effort cleanup of partially-allocated state before exit 1.
    * Disarm atexit — we are already cleaning up here. */
   s_atexit_armed = false;
   gpuRemNvidiaMemory(fd);
   for (int i = 0; i < rxAllocCnt; ++i) gpuEmuFreeBuf(rxBuffs[i], args.size);
   for (int i = 0; i < txAllocCnt; ++i) gpuEmuFreeBuf(txBuffs[i], args.size);
   ::close(stub_fd);
   dmaUnMapRegister(fd, bar0, GPU_ASYNC_CORE_SIZE);
   ::close(fd);
   return 1;
}
