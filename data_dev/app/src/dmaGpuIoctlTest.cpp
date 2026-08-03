/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    GPU ioctl coverage test for datadev built with DATA_GPU=1 and loaded
 *    against the Phase 4 emulator + nvidia_p2p_stub stack. Exercises all six
 *    GPU ioctls defined in include/GpuAsync.h.
 *
 *    Expected environment:
 *       - datadev_emulator.ko loaded
 *       - nvidia_p2p_stub.ko loaded (provides nvidia_p2p_* symbols)
 *       - datadev.ko built with NVIDIA_DRIVERS=$(pwd)/emulator/gpu_stub, loaded
 *       - /dev/datadev_0 present and gpuEn == 1 (Version register == 5)
 *
 *    GPU buffers are reserved through /dev/nvidia_p2p_stub_mem (not a plain
 *    userspace buffer) so the driver's nvidia_p2p_get_pages takes the stub
 *    addr-table reuse path; the entry then has an FD owner and is released
 *    when we close the stub fd, instead of leaking to rmmod time.
 *
 *    Exit code: 0 if all checks pass, 1 otherwise.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <argp.h>
#include <inttypes.h>
#include <errno.h>

#include <iostream>

#include <AxisDriver.h>
#include <GpuAsync.h>
#include "emu_gpu_addr_table.h"   /* STUB_RESERVE_BUF uapi (via -I gpu_stub/src) */

/* ----------------------------------------------------------------------------
 * Argp CLI (matches Phase 3 dmaIoctlTest pattern)
 * --------------------------------------------------------------------------*/

const char *argp_program_version     = "dmaGpuIoctlTest 1.0";
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

/* ----------------------------------------------------------------------------
 * Pass/fail counters + report helper (CPPLINT-safe)
 * --------------------------------------------------------------------------*/

static int gPassed = 0;
static int gErrors = 0;

static void report(const char *name, bool ok, const char *detail) {
   if (ok) {
      printf("[PASS] %-40s %s\n", name, detail ? detail : "");
      gPassed++;
   } else {
      printf("[FAIL] %-40s %s\n", name, detail ? detail : "");
      gErrors++;
   }
}

/* ----------------------------------------------------------------------------
 * Stub buffer helpers
 *
 * The datadev driver's nvidia_p2p_get_pages only takes the addr-table "reuse"
 * path -- giving the entry a stub-FD owner that is released on close -- when
 * the registered VA lies inside a /dev/nvidia_p2p_stub_mem mmap. A plain
 * posix_memalign buffer takes the fallback path, whose entry has no owner and
 * leaks (tripping WARN_ON in emu_gpu_addr_table_exit at rmmod). Reserve +
 * mmap a stub buffer, 64KB-aligned (so the driver-computed virt_offset is 0
 * and the emulator addr_lookup resolves), matching rdmaTestEmu/dmaGpuToggleTest.
 * --------------------------------------------------------------------------*/

static void *stubAllocBuf(int stub_fd, size_t size, uint32_t *out_buf_id) {
   struct stub_reserve_req req;
   memset(&req, 0, sizeof(req));
   req.size = static_cast<uint32_t>(size);

   if (ioctl(stub_fd, STUB_RESERVE_BUF, &req) < 0) {
      fprintf(stderr, "stubAllocBuf: STUB_RESERVE_BUF failed size=%zu: %s\n",
              size, strerror(errno));
      return nullptr;
   }

   long page_size = sysconf(_SC_PAGESIZE);
   if (page_size <= 0) page_size = 4096;
   off_t offset = static_cast<off_t>(req.buf_id) * page_size;

   const size_t kAlign = 64 * 1024;
   void *rsv = mmap(nullptr, size + kAlign, PROT_NONE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   if (rsv == MAP_FAILED) {
      fprintf(stderr, "stubAllocBuf: mmap reservation size=%zu failed: %s\n",
              size + kAlign, strerror(errno));
      return nullptr;
   }

   uintptr_t rsv_u   = reinterpret_cast<uintptr_t>(rsv);
   uintptr_t aligned = (rsv_u + kAlign - 1) & ~static_cast<uintptr_t>(kAlign - 1);
   size_t    head    = aligned - rsv_u;
   size_t    tail    = kAlign - head;

   if (head > 0) munmap(rsv, head);
   if (tail > 0) munmap(reinterpret_cast<void *>(aligned + size), tail);

   void *va = mmap(reinterpret_cast<void *>(aligned), size,
                   PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
                   stub_fd, offset);
   if (va == MAP_FAILED) {
      fprintf(stderr, "stubAllocBuf: mmap(buf_id=%u size=%zu) failed: %s\n",
              req.buf_id, size, strerror(errno));
      munmap(reinterpret_cast<void *>(aligned), size);
      return nullptr;
   }

   *out_buf_id = req.buf_id;
   return va;
}

static void stubFreeBuf(void *va, size_t size) {
   if (va && va != MAP_FAILED) munmap(va, size);
   /* No ioctl; the stub drops the FD-holder refcount on close(stub_fd),
    * which is also where the driver's free_callback is fired. */
}

/* ----------------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------------*/

int main(int argc, char **argv) {
   struct Args args;
   args.path = "/dev/datadev_0";

   argp_parse(&argp_parser, argc, argv, 0, 0, &args);

   int fd = open(args.path, O_RDWR);
   if (fd < 0) {
      fprintf(stderr, "open(%s) failed: %s\n", args.path, strerror(errno));
      return 1;
   }

   printf("=== dmaGpuIoctlTest: %s ===\n", args.path);

   int stub_fd = open("/dev/nvidia_p2p_stub_mem", O_RDWR);
   if (stub_fd < 0) {
      fprintf(stderr, "open(/dev/nvidia_p2p_stub_mem) failed: %s\n", strerror(errno));
      close(fd);
      return 1;
   }

   /* 1) GPU_Is_Gpu_Async_Supp -> true */
   bool supp = gpuIsGpuAsyncSupported(fd);
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "(got %s, expected true)", supp ? "true" : "false");
      report("GPU_Is_Gpu_Async_Supp", supp, buf);
   }

   /* 2) GPU_Get_Gpu_Async_Ver -> 5 */
   uint32_t ver = gpuGetGpuAsyncVersion(fd);
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "(got %u, expected 5)", ver);
      report("GPU_Get_Gpu_Async_Ver", ver == 5, buf);
   }

   /* 3) GPU_Get_Max_Buffers -> 4 */
   uint32_t maxb = gpuGetMaxBuffers(fd);
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "(got %u, expected 4)", maxb);
      report("GPU_Get_Max_Buffers", maxb == 4, buf);
   }

   /* 4) GPU_Add_Nvidia_Memory (write=1) with a stub-reserved 64KB buffer.
    *    A separate reservation per Add keeps one nvidia_p2p_get_pages (one
    *    driver-holder) per stub addr-table entry, so each is cleanly freed
    *    when the stub fd is closed. */
   uint32_t wbufId = 0;
   void *wbuf = stubAllocBuf(stub_fd, 65536, &wbufId);
   if (wbuf == nullptr) {
      close(stub_fd);
      close(fd);
      return 1;
   }
   memset(wbuf, 0, 65536);

   ssize_t rc = gpuAddNvidiaMemory(fd, 1, reinterpret_cast<uint64_t>(wbuf), 65536);
   {
      char detail[64];
      snprintf(detail, sizeof(detail), "(rc=%zd, expected 0)", rc);
      report("GPU_Add_Nvidia_Memory(write=1)", rc == 0, detail);
   }

   /* 5) GPU_Set_Write_Enable idx=0 (valid) */
   rc = gpuSetWriteEn(fd, 0);
   {
      char detail[64];
      snprintf(detail, sizeof(detail), "(rc=%zd, expected 0)", rc);
      report("GPU_Set_Write_Enable(idx=0)", rc == 0, detail);
   }

   /* 6) GPU_Set_Write_Enable idx=99 (out-of-range -> negative) */
   rc = gpuSetWriteEn(fd, 99);
   {
      char detail[80];
      snprintf(detail, sizeof(detail), "(rc=%zd, expected <0 for idx>count)", rc);
      report("GPU_Set_Write_Enable(idx=99, negative)", rc < 0, detail);
   }

   /* 7) GPU_Rem_Nvidia_Memory -> teardown */
   rc = gpuRemNvidiaMemory(fd);
   {
      char detail[64];
      snprintf(detail, sizeof(detail), "(rc=%zd, expected 0)", rc);
      report("GPU_Rem_Nvidia_Memory", rc == 0, detail);
   }

   /* 8) GPU_Add_Nvidia_Memory (write=0 -> read buffer path), fresh stub buffer */
   uint32_t rbufId = 0;
   void *rbuf = stubAllocBuf(stub_fd, 65536, &rbufId);
   if (rbuf == nullptr) {
      stubFreeBuf(wbuf, 65536);
      close(stub_fd);
      close(fd);
      return 1;
   }
   memset(rbuf, 0, 65536);

   rc = gpuAddNvidiaMemory(fd, 0, reinterpret_cast<uint64_t>(rbuf), 65536);
   {
      char detail[64];
      snprintf(detail, sizeof(detail), "(rc=%zd, expected 0)", rc);
      report("GPU_Add_Nvidia_Memory(write=0)", rc == 0, detail);
   }

   /* 9) GPU_Rem_Nvidia_Memory -> teardown #2 */
   rc = gpuRemNvidiaMemory(fd);
   {
      char detail[64];
      snprintf(detail, sizeof(detail), "(rc=%zd, expected 0)", rc);
      report("GPU_Rem_Nvidia_Memory(after read)", rc == 0, detail);
   }

   stubFreeBuf(wbuf, 65536);
   stubFreeBuf(rbuf, 65536);
   /* Closing the stub fd releases the reservations and fires the driver's
    * free_callback for each mapped buffer -> no addr-table leak at rmmod. */
   close(stub_fd);
   close(fd);

   printf("=== Summary: %d passed, %d failed ===\n", gPassed, gErrors);
   return gErrors > 0 ? 1 : 0;
}
