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
 *       - /dev/datadev_0 present and gpuEn == 1 (Version register == 4)
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

   /* 1) GPU_Is_Gpu_Async_Supp -> true */
   bool supp = gpuIsGpuAsyncSupported(fd);
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "(got %s, expected true)", supp ? "true" : "false");
      report("GPU_Is_Gpu_Async_Supp", supp, buf);
   }

   /* 2) GPU_Get_Gpu_Async_Ver -> 4 */
   uint32_t ver = gpuGetGpuAsyncVersion(fd);
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "(got %u, expected 4)", ver);
      report("GPU_Get_Gpu_Async_Ver", ver == 4, buf);
   }

   /* 3) GPU_Get_Max_Buffers -> 4 */
   uint32_t maxb = gpuGetMaxBuffers(fd);
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "(got %u, expected 4)", maxb);
      report("GPU_Get_Max_Buffers", maxb == 4, buf);
   }

   /* 4) GPU_Add_Nvidia_Memory (write=1) with a 64KB-aligned userspace buf */
   void *buf = nullptr;
   int rc_align = posix_memalign(&buf, 65536, 65536);
   if (rc_align != 0 || buf == nullptr) {
      fprintf(stderr, "posix_memalign failed: %s\n", strerror(rc_align));
      close(fd);
      return 1;
   }
   memset(buf, 0, 65536);

   ssize_t rc = gpuAddNvidiaMemory(fd, 1, reinterpret_cast<uint64_t>(buf), 65536);
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

   /* 8) GPU_Add_Nvidia_Memory (write=0 -> read buffer path) */
   rc = gpuAddNvidiaMemory(fd, 0, reinterpret_cast<uint64_t>(buf), 65536);
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

   free(buf);
   close(fd);

   printf("=== Summary: %d passed, %d failed ===\n", gPassed, gErrors);
   return gErrors > 0 ? 1 : 0;
}
