/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    Small-frame DMA loopback test with random byte payloads.  Writes frames
 *    of sizes 1 through 4 bytes filled with random data, reads them back via
 *    the emulator loopback, and compares every byte.  Exercises the DMA path
 *    at sub-word granularity where PRBS validation cannot operate (PRBS
 *    requires >= 12 bytes and 4-byte alignment).
 *
 *    Exit 0 = all sizes passed, non-zero = at least one mismatch.
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
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <argp.h>
#include <inttypes.h>

#include <AxisDriver.h>

const  char * argp_program_version     = "dmaSmallFrameTest 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

#ifndef DEFAULT_AXI_DEVICE
#define DEFAULT_AXI_DEVICE "/dev/datadev_0"
#endif

struct PrgArgs {
   const char * path;
   uint32_t     count;
   uint32_t     minSize;
   uint32_t     maxSize;
};

static struct PrgArgs DefArgs = { DEFAULT_AXI_DEVICE, 100, 1, 4 };

static char args_doc[] = "";
static char doc[]      = "Small-frame DMA loopback with random byte payloads.";

static struct argp_option options[] = {
   { "path",  'p', "PATH",  0, "Path of datadev device. Default=" DEFAULT_AXI_DEVICE ".", 0 },
   { "count", 'c', "COUNT", 0, "Frames per size (default 100).", 0 },
   { "min",   'n', "MIN",   0, "Minimum frame size in bytes (default 1).", 0 },
   { "max",   'x', "MAX",   0, "Maximum frame size in bytes (default 4).", 0 },
   { 0 }
};

error_t parseArgs(int key, char *arg, struct argp_state *state) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;
   switch (key) {
      case 'p': args->path    = arg; break;
      case 'c': args->count   = strtoul(arg, NULL, 10); break;
      case 'n': args->minSize = strtoul(arg, NULL, 10); break;
      case 'x': args->maxSize = strtoul(arg, NULL, 10); break;
      default: return ARGP_ERR_UNKNOWN;
   }
   return 0;
}

static struct argp argp = { options, parseArgs, args_doc, doc };

int main(int argc, char **argv) {
   struct PrgArgs args;
   memcpy(&args, &DefArgs, sizeof(struct PrgArgs));
   argp_parse(&argp, argc, argv, 0, 0, &args);

   int32_t fd = open(args.path, O_RDWR);
   if (fd < 0) {
      perror("open");
      return 1;
   }

   uint8_t mask[DMA_MASK_SIZE];
   dmaInitMaskBytes(mask);
   dmaAddMaskBytes(mask, 0);
   if (dmaSetMaskBytes(fd, mask) != 0) {
      printf("FAIL: dmaSetMaskBytes\n");
      close(fd);
      return 1;
   }

   srand((unsigned)time(NULL));

   uint8_t txBuf[65536];
   uint8_t rxBuf[65536];
   uint32_t rxFlags = 0;
   uint32_t rxDest  = 0;
   int failed = 0;
   fd_set fds;
   struct timeval timeout;

   for (uint32_t sz = args.minSize; sz <= args.maxSize; sz++) {
      uint32_t mismatches = 0;

      for (uint32_t i = 0; i < args.count; i++) {
         for (uint32_t b = 0; b < sz; b++)
            txBuf[b] = (uint8_t)(rand() & 0xFF);

         FD_ZERO(&fds);
         FD_SET(fd, &fds);
         timeout.tv_sec = 5;
         timeout.tv_usec = 0;
         if (select(fd + 1, NULL, &fds, NULL, &timeout) <= 0) {
            printf("FAIL: select(write) timed out at size=%u frame=%u\n", sz, i);
            failed = 1;
            break;
         }

         ssize_t wret = dmaWrite(fd, txBuf, sz, axisSetFlags(0x2, 0x0, 0), 0);
         if (wret < 0) {
            printf("FAIL: dmaWrite returned %zd at size=%u frame=%u\n", wret, sz, i);
            failed = 1;
            break;
         }

         FD_ZERO(&fds);
         FD_SET(fd, &fds);
         timeout.tv_sec = 5;
         timeout.tv_usec = 0;
         if (select(fd + 1, &fds, NULL, NULL, &timeout) <= 0) {
            printf("FAIL: select(read) timed out at size=%u frame=%u\n", sz, i);
            failed = 1;
            break;
         }

         ssize_t rret = dmaRead(fd, rxBuf, sizeof(rxBuf), &rxFlags, NULL, &rxDest);
         if (rret != (ssize_t)sz) {
            printf("FAIL: dmaRead returned %zd (expected %u) at size=%u frame=%u\n",
                   rret, sz, sz, i);
            failed = 1;
            break;
         }

         if (memcmp(txBuf, rxBuf, sz) != 0) {
            mismatches++;
            if (mismatches <= 3) {
               printf("MISMATCH at size=%u frame=%u:", sz, i);
               for (uint32_t b = 0; b < sz; b++) {
                  if (txBuf[b] != rxBuf[b])
                     printf(" byte[%u] tx=0x%02x rx=0x%02x", b, txBuf[b], rxBuf[b]);
               }
               printf("\n");
            }
         }
      }

      if (mismatches > 0) {
         printf("FAIL: size=%u  %u/%u frames had mismatches\n", sz, mismatches, args.count);
         failed = 1;
      } else if (!failed) {
         printf("PASS: size=%u  %u frames verified\n", sz, args.count);
      }
   }

   close(fd);
   return failed;
}
