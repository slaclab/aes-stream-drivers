/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    Error-path sanity test for the datadev driver. Exercises three failure
 *    modes that must be handled gracefully without a kernel panic:
 *      1. Buffer exhaustion via dmaGetIndex until no TX buffers remain
 *      2. Oversized write exceeding cfgSize
 *      3. Returning an invalid buffer index via DMA_Ret_Index
 *    Exits 0 if every case behaves as expected, 1 otherwise.
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
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <argp.h>
#include <inttypes.h>
#include <iostream>
#include <cstdio>

#include <AxisDriver.h>

using std::cout;
using std::endl;

const  char * argp_program_version     = "dmaErrorTest 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

#ifndef DEFAULT_AXI_DEVICE
#define DEFAULT_AXI_DEVICE "/dev/datadev_0"
#endif

// Upper bound on TX buffers we will try to drain in the exhaustion test.
// cfgTxCount defaults to 1024; we size the holding array with generous margin.
#define MAX_HELD_INDICES 4096

struct PrgArgs {
   const char * path;
};

static struct PrgArgs DefArgs = { DEFAULT_AXI_DEVICE };

static char args_doc[] = "";
static char doc[]      = "Exercise datadev error paths: buffer exhaustion, oversized write, invalid buffer index.";

static struct argp_option options[] = {
   { "path", 'p', "PATH", 0, "Path of datadev device to use. Default=" DEFAULT_AXI_DEVICE ".", 0 },
   { 0 }
};

error_t parseArgs(int key, char *arg, struct argp_state *state) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch (key) {
      case 'p':
         args->path = arg;
         break;
      default:
         return ARGP_ERR_UNKNOWN;
   }
   return 0;
}

static struct argp argp = { options, parseArgs, args_doc, doc };

int main(int argc, char **argv) {
   struct PrgArgs args;
   int            fd;
   int            errors = 0;
   int            passed = 0;

   memcpy(&args, &DefArgs, sizeof(struct PrgArgs));
   argp_parse(&argp, argc, argv, 0, 0, &args);

   printf("dmaErrorTest: opening %s\n", args.path);
   if ((fd = open(args.path, O_RDWR)) < 0) {
      printf("FATAL: cannot open %s (errno=%d)\n", args.path, errno);
      return 1;
   }

   ssize_t txCount  = dmaGetTxBuffCount(fd);
   ssize_t buffSize = dmaGetBuffSize(fd);
   if (txCount <= 0 || buffSize <= 0) {
      printf("FATAL: bad ioctl baseline txCount=%zd buffSize=%zd\n", txCount, buffSize);
      close(fd);
      return 1;
   }
   printf("Baseline: txCount=%zd buffSize=%zd\n", txCount, buffSize);

   // -------------------------------------------------------------------------
   // Test 1 -- Buffer exhaustion
   //   Drain all available TX buffer indices, verify subsequent acquisitions
   //   fail, then return every index so later tests are not starved.
   // -------------------------------------------------------------------------
   {
      int32_t held[MAX_HELD_INDICES];
      int32_t idx;
      int     heldCount = 0;

      while (heldCount < MAX_HELD_INDICES) {
         idx = dmaGetIndex(fd);
         if (idx < 0) {
            break;
         }
         held[heldCount++] = idx;
      }
      printf("Held %d TX buffers (txCount=%zd)\n", heldCount, txCount);

      // Driver should have handed out close to every TX buffer it owns.
      if (heldCount >= static_cast<int>(txCount * 0.8)) {
         printf("PASS: buffer exhaustion held %d / %zd (>=80%%)\n", heldCount, txCount);
         passed++;
      } else {
         printf("FAIL: buffer exhaustion held %d / %zd (<80%%)\n", heldCount, txCount);
         errors++;
      }

      // After draining, the next acquisition must fail.
      idx = dmaGetIndex(fd);
      if (idx < 0) {
         printf("PASS: dmaGetIndex after exhaustion returned %d\n", idx);
         passed++;
      } else {
         printf("FAIL: dmaGetIndex after exhaustion returned %d (expected < 0)\n", idx);
         errors++;
         // Also return the surprise buffer so we do not leak it.
         held[heldCount++] = idx;
      }

      // Verify the driver's in-user accounting matches what we hold.
      ssize_t inUser = ioctl(fd, DMA_Get_TxBuffinUser_Count, 0);
      if (inUser == heldCount) {
         printf("PASS: DMA_Get_TxBuffinUser_Count = %zd matches held=%d\n", inUser, heldCount);
         passed++;
      } else {
         printf("FAIL: DMA_Get_TxBuffinUser_Count = %zd, expected %d\n", inUser, heldCount);
         errors++;
      }

      // Return every held buffer to avoid starving subsequent tests.
      for (int i = 0; i < heldCount; i++) {
         dmaRetIndex(fd, held[i]);
      }
   }

   // -------------------------------------------------------------------------
   // Test 2 -- Oversized write
   //   A write larger than cfgSize must be rejected by Dma_Write without
   //   crashing the kernel.
   // -------------------------------------------------------------------------
   {
      size_t allocSize = static_cast<size_t>(buffSize) + 4096;
      void * buf       = malloc(allocSize);
      if (buf == NULL) {
         printf("FAIL: oversized write -- malloc(%zu) returned NULL\n", allocSize);
         errors++;
      } else {
         struct DmaWriteData wr;
         memset(&wr, 0, sizeof(wr));
         wr.size  = static_cast<uint32_t>(buffSize) + 1;  // strictly larger than cfgSize
         wr.is32  = (sizeof(void *) == 4);
         wr.data  = (uint64_t)buf;
         wr.dest  = 0;
         wr.flags = 0;

         ssize_t ret = write(fd, &wr, sizeof(wr));
         if (ret < 0) {
            printf("PASS: oversized write rejected (ret=%zd errno=%d)\n", ret, errno);
            passed++;
         } else {
            printf("FAIL: oversized write returned %zd (expected < 0)\n", ret);
            errors++;
         }
         free(buf);
      }
   }

   // -------------------------------------------------------------------------
   // Test 3 -- Invalid buffer index
   //   Hand a clearly out-of-range index to DMA_Ret_Index. The driver's
   //   dmaGetBufferList lookup returns NULL for invalid indices and the
   //   ioctl must fail rather than panic.
   // -------------------------------------------------------------------------
   {
      uint32_t bad_idx = 99999;
      // DmaDriver.h encodes count=1 in the upper 16 bits of the command.
      uint32_t cmd     = DMA_Ret_Index | 0x10000;

      ssize_t ret = ioctl(fd, cmd, &bad_idx);
      if (ret != 0) {
         printf("PASS: invalid index %u rejected (ret=%zd errno=%d)\n", bad_idx, ret, errno);
         passed++;
      } else {
         printf("FAIL: invalid index %u returned 0 (expected non-zero)\n", bad_idx);
         errors++;
      }
   }

   printf("\nError-path test: %d passed, %d failed\n", passed, errors);

   close(fd);
   return errors > 0 ? 1 : 0;
}
