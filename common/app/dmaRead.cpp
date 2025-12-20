/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 * This program will open up a AXIS DMA port and attempt to read data.
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
#include <argp.h>
#include <inttypes.h>
#include <iostream>
#include <cstdio>

#include <AxisDriver.h>
#include <PrbsData.h>
#include <AppUtils.h>

#ifndef DEFAULT_AXI_DEVICE
#define DEFAULT_AXI_DEVICE "/dev/datadev_0"
#endif

using std::cout;
using std::endl;

static int please_exit;

const  char * argp_program_version = "dmaRead 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

struct PrgArgs {
   const char * path;
   const char * dest;
   uint32_t     prbsDis;
   uint32_t     idxEn;
   uint32_t     rawEn;
   bool         dumpHdr;
   bool         verbose;
   bool         wait;
   int64_t      count;
};

static struct PrgArgs DefArgs = { DEFAULT_AXI_DEVICE, NULL, 1, 0x0, 0, false, false, false, -1 };

static char   args_doc[] = "";
static char   doc[]      = "";

static struct argp_option options[] = {
   { "path",    'p', "PATH",   0, "Path of pgpcard device to use. Default=" DEFAULT_AXI_DEVICE ".", 0},
   { "dest",    'm', "LIST",   0, "Comma seperated list of destinations.", 0},
   { "prbs",    'e', 0,        0, "Enable PRBS checking.", 0},
   { "indexen", 'i', 0,        0, "Use index based receive buffers.", 0},
   { "rawEn",   'r', "COUNT",  0, "Show raw data up to count.", 0},
   { "dumpHdr", 'b', 0,        0, "Decode and dump transaction header.", 0},
   { "verbose", 'v', 0,        0, "Enable verbose printing.", 0},
   { "wait",    'w', 0,        0, "Wait for data to be ready with select().", 0},
   { "count",   'c', "COUNT",  0, "Number of events to receive before exiting. -1 for infinite", 0},
   {0}
};

error_t parseArgs(int key,  char *arg, struct argp_state *state) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch (key) {
      case 'p': args->path = arg; break;
      case 'm': args->dest = arg; break;
      case 'd': args->prbsDis = 0; break;
      case 'i': args->idxEn = 1; break;
      case 'r': args->rawEn = strtol(arg, NULL, 10); break;
      case 'b': args->dumpHdr = true; break;
      case 'v': args->verbose = true; break;
      case 'w': args->wait = true; break;
      case 'c': args->count = strtol(arg, NULL, 10); break;
      default: return ARGP_ERR_UNKNOWN; break;
   }
   return(0);
}

static struct argp argp = {options, parseArgs, args_doc, doc};

extern "C" void sigintHandler(int sig) {
   please_exit = 1;
}

static void printResults(int64_t count, uint64_t totalBytes, double elapsed);

int main(int argc, char **argv) {
   uint8_t       mask[DMA_MASK_SIZE];
   int32_t       s;
   ssize_t       ret;
   int64_t       count;
   fd_set        fds;
   void *        rxData = NULL;
   uint32_t      maxSize;
   uint32_t      rxDest;
   uint32_t      rxFlags = 0;
   PrbsData      prbs(32, 4, 1, 2, 6, 31);
   bool          prbRes;
   void **       dmaBuffers = NULL;
   uint32_t      dmaSize;
   uint32_t      dmaCount = 1;
   uint32_t      x;
   uint64_t      totalBytes = 0;
   char *        tok;
   char          tBuff[100];
   double        lastUpdate = curTime();

   struct PrgArgs args = DefArgs;

   struct timeval timeout;

   argp_parse(&argp, argc, argv, 0, 0, &args);

   if ((s = open(args.path, O_RDWR)) < 0) {
      printf("Error opening %s\n", args.path);
      return 1;
   }

   dmaInitMaskBytes(mask);
   if (args.dest == NULL) {
      memset(mask, 0xFF, DMA_MASK_SIZE);
   } else {
      strcpy(tBuff, args.dest);
      tok = strtok(tBuff, ",");
      while ( tok != NULL ) {
         x = strtoul(tok, NULL, 10);
         dmaAddMaskBytes(mask, x);
         printf("Adding destination %i\n", x);
         tok = strtok(NULL, ",");
      }
   }
   dmaSetMaskBytes(s, mask);
   maxSize = 1024*1024*2;

   if (args.idxEn) {
      if ( (dmaBuffers = dmaMapDma(s, &dmaCount, &dmaSize)) == NULL ) {
         printf("Failed to map dma buffers!\n");
         return 0;
      }
      if (args.verbose) {
         printf("Mapped %u buffers of %u bytes (%.2f MB total)\n",
            dmaCount, dmaSize, double(dmaCount * dmaSize) / 1e6);
      }
   } else {
      if ((rxData = malloc(maxSize)) == NULL) {
         printf("Failed to allocate rxData!\n");
         return 0;
      }
   }
   
   // Register a signal handler to intercept SIGINT, so we can print final results
   signal(SIGINT, sigintHandler);

   double startTime = curTime();

   uint32_t* indexes = new uint32_t[dmaCount];
   uint32_t* flags = new uint32_t[dmaCount];
   uint32_t* errors = new uint32_t[dmaCount];
   uint32_t* dests = new uint32_t[dmaCount];
   int32_t* rets = new int32_t[dmaCount];

   count  = 0;
   prbRes = 0;
   do {
      memset(rets, 0, sizeof(int32_t) * dmaCount);

      // Wait for data to be ready if requested
      if (args.wait) {
         // Setup fds for select call
         FD_ZERO(&fds);
         FD_SET(s, &fds);

         // Setup select timeout for 1 second
         timeout.tv_sec = 2;
         timeout.tv_usec = 0;

         // Wait for Socket data ready
         ret = select(s+1, &fds, NULL, NULL, &timeout);
         if (ret <= 0) {
            printf("Read timeout\n");
            continue;
         }
      }

      // DMA Read
      if (args.idxEn) {
         ret = dmaReadBulkIndex(s, dmaCount, rets, indexes, flags, errors, dests);
      } else {
         ret = dmaRead(s, rxData, maxSize, &rxFlags, NULL, &rxDest);
         rets[0] = int32_t(ret);
      }

      for (uint32_t i = 0; i < dmaCount; ++i) {
         if (rets[i] <= 0) {
            if (rets[i] < 0)
               printf("Read failed: %s (%d)\n", strerror(-rets[i]), rets[i]);
            break;
         }

         if (args.idxEn) {
            rxData = dmaBuffers[indexes[i]];
            rxFlags = flags[i];
            rxDest = dests[i];
         }

         count++;
         totalBytes += rets[i];

         // Dump data for debugging purposes
         if (args.rawEn) {
            printf("Raw Data: ");
            dumpBytes(rxData, MIN(args.rawEn, count));
         }

         // Validate PBRS data
         if (args.prbsDis == 0) {
            prbRes = prbs.processData(rxData, rets[i]);
         }

         // Dump header if requested
         if (args.dumpHdr) {
            printf("Read ret=%li, Dest=%i, Fuser=0x%.2x, Luser=0x%.2x, prbs=%i, count=%" PRId64 "\n",
               ret, rxDest, axisGetFuser(rxFlags), axisGetLuser(rxFlags), prbRes, count);
         }
      }

      if (args.idxEn)
         dmaRetIndexes(s, dmaCount, indexes);

      // Print updates every so often
      if (count % 2048 == 0 && curTime() - lastUpdate > 2.5) {
         double cur = curTime();
         printResults(count, totalBytes, cur - startTime);
         lastUpdate = cur;
      }
   } while ((args.count == -1 || count < args.count) && !please_exit);

   double elapsed = curTime() - startTime;

   // Print final results
   printResults(count, totalBytes, elapsed);

   delete [] indexes;
   delete [] errors;
   delete [] flags;
   delete [] dests;

   if (args.idxEn) {
      dmaUnMapDma(s, dmaBuffers);
   } else {
      free(rxData);
   }

   close(s);
   return 0;
}

static void printResults(int64_t count, uint64_t totalBytes, double elapsed) {
   printf("\n");
   printf("Total Rx Events  : %" PRId64 "\n", count);
   printf("Total Rx Bytes   : %" PRIu64 " (%.2f GB)\n", totalBytes, double(totalBytes) / 1e9);
   printf("Rx Rate          : %.2f Hz (%.2f kHz)\n", double(count) / elapsed, double(count) / elapsed / 1024.);
   printf("Rx Speed         : %.f B/s (%.2f MB/s)\n",
      double(totalBytes) / elapsed, double(totalBytes) / elapsed / 1e6);
   printf("Elapsed:         : %.2f seconds\n", elapsed);
}
