/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    Error-path sanity test for the datadev driver. Exercises five failure
 *    modes that must be handled gracefully without a kernel panic:
 *      1. Buffer exhaustion via dmaGetIndex until no TX buffers remain
 *      2. Oversized write exceeding cfgSize
 *      3. Returning an invalid buffer index via DMA_Ret_Index
 *      4. Write to a destination one past the end of the destination mask
 *      5. Batch index return with a trailing invalid index must not strand
 *         the RX buffers named ahead of it
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

// Number of loopback frames held by index in the batch return test.
#define RET_BATCH_FRAMES 4

struct PrgArgs {
   const char * path;
};

static struct PrgArgs DefArgs = { DEFAULT_AXI_DEVICE };

static char args_doc[] = "";
static char doc[]      = "Exercise datadev error paths: buffer exhaustion, oversized write, invalid buffer index, out-of-range destination, partially invalid index batch.";

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
         // Surprise buffer: if held[] still has room, track it so the later
         // return loop covers it; otherwise hand it back directly. The
         // explicit bound check avoids a stack-buffer overflow on a driver
         // that hands out more than MAX_HELD_INDICES buffers — the while
         // loop above clamps at MAX_HELD_INDICES so heldCount==MAX is the
         // reachable state where the unchecked `held[heldCount++] = idx`
         // would write past the array.
         if (heldCount < MAX_HELD_INDICES) {
            held[heldCount++] = idx;
         } else {
            dmaRetIndex(fd, idx);
         }
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

   // -------------------------------------------------------------------------
   // Test 4 -- Out-of-range destination
   //   The destination mask holds 8 * DMA_MASK_SIZE bits, so the last valid
   //   destination is 8 * DMA_MASK_SIZE - 1. A write to exactly
   //   8 * DMA_MASK_SIZE must be rejected by the bound check in Dma_Write;
   //   if the check lets it through, the mask lookup indexes one byte past
   //   the end of the destination mask array.
   // -------------------------------------------------------------------------
   {
      uint8_t  payload[16];
      uint32_t badDest = 8 * DMA_MASK_SIZE;

      memset(payload, 0, sizeof(payload));
      ssize_t ret = dmaWrite(fd, payload, sizeof(payload), 0, badDest);
      if (ret < 0) {
         printf("PASS: write to dest %u rejected (ret=%zd errno=%d)\n", badDest, ret, errno);
         passed++;
      } else {
         printf("FAIL: write to dest %u returned %zd (expected < 0)\n", badDest, ret);
         errors++;
      }
   }

   // -------------------------------------------------------------------------
   // Test 5 -- Batch index return with a trailing invalid index
   //   Loop a few frames back, hold them by index, then return them in one
   //   dmaRetIndexes() call that ends with an out-of-range index. The call
   //   must fail, and the RX buffers named ahead of the bad entry must still
   //   go back to the hardware: Dma_Ret_Indexes has already cleared their
   //   userHas by the time it reaches the bad entry, so bailing out without
   //   retRxBuffer() leaves them owned by nobody, which shows up as a
   //   non-zero DMA_Get_RxBuffMiss_Count.
   // -------------------------------------------------------------------------
   {
      uint8_t  mask[DMA_MASK_SIZE];
      uint8_t  payload[64];
      uint32_t indexes[RET_BATCH_FRAMES + 1];
      uint32_t held = 0;
      fd_set   fds;
      struct timeval timeout;

      memset(payload, 0xA5, sizeof(payload));
      dmaInitMaskBytes(mask);
      dmaAddMaskBytes(mask, 0);

      if (dmaSetMaskBytes(fd, mask) != 0) {
         printf("FAIL: batch return -- dmaSetMaskBytes failed (errno=%d)\n", errno);
         errors++;
      } else {
         while (held < RET_BATCH_FRAMES) {
            if (dmaWrite(fd, payload, sizeof(payload), axisSetFlags(0x2, 0x0, 0), 0) < 0) break;

            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            timeout.tv_sec  = 5;
            timeout.tv_usec = 0;
            if (select(fd + 1, &fds, NULL, NULL, &timeout) <= 0) break;

            if (dmaReadIndex(fd, &indexes[held], NULL, NULL, NULL) <= 0) break;
            held++;
         }

         if (held != RET_BATCH_FRAMES) {
            printf("FAIL: batch return -- looped back %u / %d frames\n", held, RET_BATCH_FRAMES);
            errors++;
            if (held > 0) dmaRetIndexes(fd, held, indexes);
         } else {
            // TX indexes run 0..txCount-1 and RX indexes follow, so the total
            // buffer count is the first index that names no buffer.
            indexes[held] = static_cast<uint32_t>(dmaGetBuffCount(fd));

            ssize_t missBefore = dmaGetRxBuffMissCount(fd);
            ssize_t ret        = dmaRetIndexes(fd, held + 1, indexes);
            ssize_t missAfter  = dmaGetRxBuffMissCount(fd);

            if (ret < 0) {
               printf("PASS: batch with invalid index %u rejected (ret=%zd errno=%d)\n", indexes[held], ret, errno);
               passed++;
            } else {
               printf("FAIL: batch with invalid index %u returned %zd (expected < 0)\n", indexes[held], ret);
               errors++;
            }

            if (missAfter == missBefore) {
               printf("PASS: DMA_Get_RxBuffMiss_Count unchanged at %zd after rejected batch\n", missAfter);
               passed++;
            } else {
               printf("FAIL: DMA_Get_RxBuffMiss_Count went %zd -> %zd after rejected batch (RX buffers stranded)\n",
                      missBefore, missAfter);
               errors++;
            }
         }
      }
   }

   printf("\nError-path test: %d passed, %d failed\n", passed, errors);

   close(fd);
   return errors > 0 ? 1 : 0;
}
