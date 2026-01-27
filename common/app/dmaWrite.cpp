/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    Opens an AXIS DMA port to write data using the aes_stream_drivers package.
 *-----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to the
 * license terms in the LICENSE.txt file found in the top-level directory of
 * this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the aes_stream_drivers package, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 *-----------------------------------------------------------------------------
**/

#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <argp.h>
#include <iostream>
#include <cstdio>

#include <DmaDriver.h>
#include <PrbsData.h>
#include <AppUtils.h>

using std::cout;
using std::endl;

#ifndef DEFAULT_AXI_DEVICE
#define DEFAULT_AXI_DEVICE "/dev/datadev_0"
#endif

const char *argp_program_version = "dmaWrite 1.0";
const char *argp_program_bug_address = "rherbst@slac.stanford.edu";

// Program arguments structure
struct PrgArgs {
   const char *path;
   uint32_t    dest;
   uint32_t    size;
   long        count;
   uint32_t    prbsEn;
   uint32_t    idxEn;
};

// Default program arguments
static struct PrgArgs DefArgs = { DEFAULT_AXI_DEVICE, 0, 1000, -1, 1, 0 };

// Documentation for arguments
static char args_doc[] = "dest";
static char doc[] = "Dest is passed as an integer.";

// Options structure
static struct argp_option options[] = {
   { "path",    'p', "PATH",   0, "Path of datadev device to use. Default=" DEFAULT_AXI_DEVICE ".", 0 },
   { "prbsen",  'e', 0,        0, "Enable PRBS generation.", 0 },
   { "size",    's', "SIZE",   0, "Size of data to generate. Default=1000", 0 },
   { "count",   'c', "COUNT",  0, "Number of frames to generate, if -1, run forever. Default=-1", 0 },
   { "indexen", 'i', 0,        0, "Use index based transmit buffers.", 0 },
   { 0 }
};

// Argument parsing function
error_t parseArgs(int key, char *arg, struct argp_state *state) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch (key) {
      case 'p': args->path = arg; break;
      case 'e': args->prbsEn = 1; break;
      case 's': args->size = strtol(arg, NULL, 10); break;
      case 'c': args->count = strtol(arg, NULL, 10); break;
      case 'i': args->idxEn = 1; break;
      case ARGP_KEY_ARG:
         if (state->arg_num == 0) {
            args->dest = strtol(arg, NULL, 10);
         } else {
            argp_usage(state);
         }
         break;
      case ARGP_KEY_END:
         if (state->arg_num < 1) argp_usage(state);
         break;
      default:
         return ARGP_ERR_UNKNOWN;
   }
   return 0;
}

// Definition of the argp structure to parse command line arguments
static struct argp argp = { options, parseArgs, args_doc, doc };

static bool please_exit = false;
extern "C" void sigintHandler(int sig) {
   please_exit = 1;
}

int main(int argc, char **argv) {
   int32_t s;
   ssize_t ret;
   uint32_t count;
   fd_set fds;
   void *txData = NULL;
   PrbsData prbs(32, 4, 1, 2, 6, 31);  // Example PRBS (Pseudo-Random Binary Sequence) generator initialization
   void **dmaBuffers = NULL;
   uint32_t dmaSize;
   uint32_t dmaCount;
   int32_t dmaIndex = -1;
   bool prbValid;
   struct timeval timeout;
   struct PrgArgs args;
   uint64_t totalBytes = 0;

   // Initialize program arguments with default values
   memcpy(&args, &DefArgs, sizeof(struct PrgArgs));

   // Parse command line arguments
   argp_parse(&argp, argc, argv, 0, 0, &args);

   // Open device or file
   if ((s = open(args.path, O_RDWR)) <= 0) {
      printf("Error opening %s\n", args.path);
      return 1;
   }

   // DMA or regular buffer setup based on idxEn flag
   if (args.idxEn) {
      if ((dmaBuffers = dmaMapDma(s, &dmaCount, &dmaSize)) == NULL) {
         printf("Failed to map dma buffers!\n");
         return 0;
      }
   } else {
      if ((txData = malloc(args.size)) == NULL) {
         printf("Failed to allocate txData!\n");
         return 0;
      }
   }

   prbValid = false;
   count = 0;

   double start = curTime();
   double lastUpdate = curTime();

   signal(SIGINT, sigintHandler);

   do {
      // Setup file descriptor set for select call
      FD_ZERO(&fds);
      FD_SET(s, &fds);

      // Setup select timeout for 2 seconds
      timeout.tv_sec = 2;
      timeout.tv_usec = 0;

      // Wait for socket or file descriptor to be ready for writing
      ret = select(s + 1, NULL, &fds, NULL, &timeout);
      if (ret <= 0) {
         perror("select");
      } else {
         // If using DMA, get next buffer index
         if (args.idxEn) {
            dmaIndex = dmaGetIndex(s);
            if (dmaIndex < 0) continue;
            txData = dmaBuffers[dmaIndex];
         }

         // Generate and write data if PRBS is enabled and data is not yet valid
         if (args.prbsEn && !prbValid) {
            prbs.genData(txData, args.size);
            prbValid = true;
         }

         // Perform the write operation, using DMA if enabled
         if (args.idxEn) {
            ret = dmaWriteIndex(s, dmaIndex, args.size, 0, args.dest);
         } else {
            ret = dmaWrite(s, txData, args.size, 0, args.dest);
         }

         // On successful write, reset prbValid flag and increment count
         if (ret > 0) {
            prbValid = false;
            count++;
            totalBytes += args.size;
         }

         // Print stats every so often
         if (count % 2048 == 0 && curTime() - lastUpdate >= 2.5) {
            const double cur = curTime();
            printResults("Tx", count, totalBytes, cur - start);
            lastUpdate = cur;
         }
      }
   } while ((args.count == -1 || count < args.count) && !please_exit);

   double duration = curTime() - start;

   // Clean up allocated resources
   if (args.idxEn) {
      dmaUnMapDma(s, dmaBuffers);
   } else {
      free(txData);
   }

   // Final stats update
   printResults("Tx", count, totalBytes, duration);

   close(s);
   return 0;
}
