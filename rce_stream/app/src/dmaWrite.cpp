/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 * Program to send data on a destination. Data is prbs
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
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <argp.h>
#include <iostream>

#include <AxisDriver.h>
#include <PrbsData.h>

using std::cout;
using std::endl;

const  char * argp_program_version = "dmaWrite 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

struct PrgArgs {
   const char * path;
   uint32_t     dest;
   uint32_t     size;
   uint32_t     fuser;
   uint32_t     luser;
   uint32_t     count;
   uint32_t     prbsDis;
   uint32_t     idxEn;
   uint32_t     rawEn;
};

static struct PrgArgs DefArgs = { "/dev/axi_stream_dma_0", 0, 1000, 0x2, 0x0, 1, 0, 0 };

static char   args_doc[] = "dest";
static char   doc[]      = "   Destination is passed as integers.";

static struct argp_option options[] = {
   { "path",    'p', "PATH",   OPTION_ARG_OPTIONAL, "Path of AXI stream to use. Default=/dev/axi_stream_dma_0.", 0},
   { "prbsdis", 'd', 0,        OPTION_ARG_OPTIONAL, "Disable PRBS generation.", 0},
   { "size",    's', "SIZE",   OPTION_ARG_OPTIONAL, "Size of data to generate. Default=1000", 0},
   { "count",   'c', "COUNT",  OPTION_ARG_OPTIONAL, "Number of frames to generate. Default=1", 0},
   { "fuser",   'f', "FUSER",  OPTION_ARG_OPTIONAL, "Value for first user field in hex. Default=0x2", 0},
   { "luser",   'l', "LUSER",  OPTION_ARG_OPTIONAL, "Value for last user field in hex. Default=0x0", 0},
   { "indexen", 'i', 0,        OPTION_ARG_OPTIONAL, "Use index based transmit buffers.", 0},
   { "rawEn",   'r', "COUNT",  OPTION_ARG_OPTIONAL, "Show raw data up to count.", 0},
   {0}
};

error_t parseArgs(int key,  char *arg, struct argp_state *state) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch (key) {
      case 'p': args->path = arg; break;
      case 'd': args->prbsDis = 1; break;
      case 's': args->size = strtol(arg, NULL, 10); break;
      case 'c': args->count = strtol(arg, NULL, 10); break;
      case 'f': args->fuser = strtol(arg, NULL, 16); break;
      case 'l': args->luser = strtol(arg, NULL, 16); break;
      case 'i': args->idxEn = 1; break;
      case 'r': args->rawEn = strtol(arg, NULL, 10); break;
      case ARGP_KEY_ARG:
          switch (state->arg_num) {
             case 0: args->dest = strtol(arg, NULL, 10); break;
             default: argp_usage(state); break;
          }
          break;
      case ARGP_KEY_END:
          if ( state->arg_num < 1) argp_usage(state);
          break;
      default: return ARGP_ERR_UNKNOWN; break;
   }
   return(0);
}

static struct argp argp = {options, parseArgs, args_doc, doc};

int main(int argc, char **argv) {
   int32_t       s;
   int32_t       ret;
   uint32_t      count;
   fd_set        fds;
   void *        txData;
   PrbsData      prbs(32, 4, 1, 2, 6, 31);
   void **       dmaBuffers;
   uint32_t      dmaSize;
   uint32_t      dmaCount;
   int32_t       dmaIndex;
   bool          prbValid;
   uint32_t      x;

   struct timeval timeout;
   struct PrgArgs args;

   memcpy(&args, &DefArgs, sizeof(struct PrgArgs));
   argp_parse(&argp, argc, argv, 0, 0, &args);

   if ( (s = open(args.path, O_RDWR)) <= 0 ) {
      printf("Error opening %s\n", args.path);
      return(1);
   }

   if ( args.idxEn ) {
      if ( (dmaBuffers = dmaMapDma(s, &dmaCount, &dmaSize)) == NULL ) {
         printf("Failed to map dma buffers!\n");
         return(0);
      }
   } else {
      if ((txData = malloc(args.size)) == NULL) {
         printf("Failed to allocate rxData!\n");
         return(0);
      }
   }

   prbValid = false;
   count = 0;
   do {
      // Setup fds for select call
      FD_ZERO(&fds);
      FD_SET(s, &fds);

      // Setup select timeout for 1 second
      timeout.tv_sec = 2;
      timeout.tv_usec = 0;

      // Wait for Socket data ready
      ret = select(s+1, NULL, &fds, NULL, &timeout);
      if ( ret <= 0 ) {
         printf("Write timeout\n");
      } else {
         if ( args.idxEn ) {
            dmaIndex = dmaGetIndex(s);
            if ( dmaIndex < 0 ) continue;
            txData = dmaBuffers[dmaIndex];
         }

         // Gen data
         if ( args.prbsDis == 0 && !prbValid ) {
            prbs.genData(txData, args.size);
            prbValid = true;
         }

         // DMA Write
         if ( args.idxEn ) {
             ret = dmaWriteIndex(s, dmaIndex, args.size, axisSetFlags(args.fuser, args.luser, 0), args.dest);
         } else {
             ret = dmaWrite(s, txData, args.size, axisSetFlags(args.fuser, args.luser, 0), args.dest);
         }

         if ( ret > 0 ) {
            prbValid = false;
            count++;
            printf("Write ret=%i, Dest=%i, Fuser=0x%.2x, Luser=0x%.2x, count=%i\n", ret, args.dest, args.fuser, args.luser, count);
            if ( args.rawEn ) {
               printf("Raw Data: ");
               for (x = 0; x < args.rawEn; x++) {
                  printf("0x%.2x ", ((uint8_t *)txData)[x]);
                  if ( ((x+1) % 10) == 0 ) printf("\n          ");
               }
               printf("\n");
            }
         } else if ( ret < 0 ) {
             printf("Write error!\n");
         }
      }
   } while ( count < args.count );

   if ( args.idxEn ) {
       dmaUnMapDma(s, dmaBuffers);
   } else {
       free(txData);
   }

   close(s);
   return(0);
}
