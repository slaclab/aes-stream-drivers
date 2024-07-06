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
#include <iostream>

#include <AxisDriver.h>
#include <PrbsData.h>

using std::cout;
using std::endl;

const  char * argp_program_version = "pgpRead 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

struct PrgArgs {
   const char * path;
   const char * dest;
   uint32_t     prbsDis;
   uint32_t     idxEn;
   uint32_t     rawEn;
};

static struct PrgArgs DefArgs = { "/dev/axi_stream_dma_0", NULL, 0x0, 0x0, 0 };

static char   args_doc[] = "";
static char   doc[]      = "";

static struct argp_option options[] = {
   { "path",    'p', "PATH",   OPTION_ARG_OPTIONAL, "Path of pgpcard device to use. Default=/dev/axi_stream_dma_0.", 0},
   { "dest",    'm', "LIST",   OPTION_ARG_OPTIONAL, "Comma seperated list of destinations.", 0},
   { "prbsdis", 'd', 0,        OPTION_ARG_OPTIONAL, "Disable PRBS checking.", 0},
   { "indexen", 'i', 0,        OPTION_ARG_OPTIONAL, "Use index based receive buffers.", 0},
   { "rawEn",   'r', "COUNT",  OPTION_ARG_OPTIONAL, "Show raw data up to count.", 0},
   {0}
};

error_t parseArgs(int key,  char *arg, struct argp_state *state) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch (key) {
      case 'p': args->path = arg; break;
      case 'm': args->dest = arg; break;
      case 'd': args->prbsDis = 1; break;
      case 'i': args->idxEn = 1; break;
      case 'r': args->rawEn = strtol(arg, NULL, 10); break;
      default: return ARGP_ERR_UNKNOWN; break;
   }
   return(0);
}

static struct argp argp = {options, parseArgs, args_doc, doc};

int main(int argc, char **argv) {
   uint8_t       mask[DMA_MASK_SIZE];
   int32_t       s;
   int32_t       ret;
   int32_t       count;
   fd_set        fds;
   void *        rxData;
   uint32_t      maxSize;
   uint32_t      rxDest;
   uint32_t      rxFuser;
   uint32_t      rxLuser;
   uint32_t      rxFlags;
   PrbsData      prbs(32, 4, 1, 2, 6, 31);
   bool          prbRes;
   void **       dmaBuffers;
   uint32_t      dmaSize;
   uint32_t      dmaCount;
   uint32_t      dmaIndex;
   uint32_t      x;
   char *        tok;
   char          tBuff[100];

   struct PrgArgs args;

   struct timeval timeout;

   memcpy(&args, &DefArgs, sizeof(struct PrgArgs));
   argp_parse(&argp, argc, argv, 0, 0, &args);

   if ( (s = open(args.path, O_RDWR)) <= 0 ) {
      printf("Error opening %s\n", args.path);
      return(1);
   }

   dmaInitMaskBytes(mask);
   if ( args.dest == NULL ) {
      memset(mask, 0xFF, DMA_MASK_SIZE);
   } else {
      strcpy(tBuff,args.dest);//NOLINT
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

   if ( args.idxEn ) {
      if ( (dmaBuffers = dmaMapDma(s, &dmaCount, &dmaSize)) == NULL ) {
         printf("Failed to map dma buffers!\n");
         return(0);
      }
   } else {
      if ((rxData = malloc(maxSize)) == NULL) {
         printf("Failed to allocate rxData!\n");
         return(0);
      }
   }

   count  = 0;
   prbRes = 0;
   do {
      // Setup fds for select call
      FD_ZERO(&fds);
      FD_SET(s, &fds);

      // Setup select timeout for 1 second
      timeout.tv_sec = 2;
      timeout.tv_usec = 0;

      // Wait for Socket data ready
      ret = select(s+1, &fds, NULL, NULL, &timeout);
      if ( ret <= 0 ) {
         printf("Read timeout\n");
      } else {
         // DMA Read
         if ( args.idxEn ) {
            ret = dmaReadIndex(s, &dmaIndex, &rxFlags, NULL, &rxDest);
            rxData = dmaBuffers[dmaIndex];
         } else {
             ret = dmaRead(s, rxData, maxSize, &rxFlags, NULL, &rxDest);
         }

         rxFuser = axisGetFuser(rxFlags);
         rxLuser = axisGetFuser(rxFlags);

         if ( ret > 0 ) {
            if ( args.prbsDis == 0 ) prbRes = prbs.processData(rxData, ret);
            if ( args.idxEn ) dmaRetIndex(s, dmaIndex);

            count++;
            printf("Read ret=%i, Dest=%i, Fuser=0x%.2x, Luser=0x%.2x, prbs=%i, count=%i\n", ret, rxDest, rxFuser, rxLuser, prbRes, count);
            if ( args.rawEn ) {
               printf("Raw Data: ");
               for (x = 0; x < args.rawEn; x++) {
                  printf("0x%.2x ", ((uint8_t *)rxData)[x]);
                  if ( ((x+1) % 10) == 0 ) printf("\n          ");
               }
               printf("\n");
            }
         }
      }
   } while ( 1 );

   if ( args.idxEn ) {
       dmaUnMapDma(s, dmaBuffers);
   } else {
       free(rxData);
   }

   close(s);
   return(0);
}

