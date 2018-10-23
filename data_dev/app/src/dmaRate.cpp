/**
 *-----------------------------------------------------------------------------
 * Title      : DMA read utility
 * ----------------------------------------------------------------------------
 * File       : dmaRate.cpp
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
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
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <argp.h>
#include <AxisDriver.h>
#include <PrbsData.h>
using namespace std;

#define MAX_RET_CNT_C 1000

const  char * argp_program_version = "dmaRate 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

struct PrgArgs {
   const char * path;
   uint32_t     count;
};

static struct PrgArgs DefArgs = { "/dev/datadev_0",10000000 };

static char   args_doc[] = "";
static char   doc[]      = "";

static struct argp_option options[] = {
   { "path",    'p', "PATH",   OPTION_ARG_OPTIONAL, "Path of pgpcard device to use. Default=/dev/axi_stream_dma_0.",0},
   { "count",   'c', "COUNT",  OPTION_ARG_OPTIONAL, "Total iterations",0},
   {0}
};

error_t parseArgs ( int key,  char *arg, struct argp_state *state ) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch(key) {
      case 'p': args->path   = arg; break;
      case 'c': args->count  = atoi(arg); break;
      default: return ARGP_ERR_UNKNOWN; break;
   }
   return(0);
}

static struct argp argp = {options,parseArgs,args_doc,doc};

int main (int argc, char **argv) {
   uint8_t       mask[DMA_MASK_SIZE];
   int32_t       ret;
   int32_t       s;
   uint32_t      rxFlags[MAX_RET_CNT_C];
   PrbsData      prbs(32,4,1,2,6,31);
   void **       dmaBuffers;
   uint32_t      dmaSize;
   uint32_t      dmaCount;
   uint32_t      dmaIndex[MAX_RET_CNT_C];
   int32_t       dmaRet[MAX_RET_CNT_C];
   int32_t       x;
   float         last;
   float         rate;
   float         bw;
   float         duration;
   int32_t       min;
   int32_t       max;
   int32_t       total;

   uint32_t      getCnt = MAX_RET_CNT_C;

   struct timeval sTime;
   struct timeval eTime;
   struct timeval dTime;

   struct PrgArgs args;

   memcpy(&args,&DefArgs,sizeof(struct PrgArgs));
   argp_parse(&argp,argc,argv,0,0,&args);

   printf("  minCnt        maxCnt        size      count   duration       rate         bw\n");

   dmaInitMaskBytes(mask);
   memset(mask,0xFF,DMA_MASK_SIZE);

   if ( (s = open(args.path, O_RDWR)) <= 0 ) {
      printf("Error opening %s\n",args.path);
      return(1);
   }

   if ( (dmaBuffers = dmaMapDma(s,&dmaCount,&dmaSize)) == NULL ) {
      printf("Failed to map dma buffers!\n");
      return(0);
   }

   dmaSetMaskBytes(s,mask);

   while(1) {

      bw     = 0.0;
      rate   = 0.0;
      last   = 0.0;
      max    = 0;
      min    = 8000;
      total  = 0;
      gettimeofday(&sTime,NULL);

      while ( rate < args.count ) {

         // DMA Read
         ret = dmaReadBulkIndex(s,getCnt,dmaRet,dmaIndex,rxFlags,NULL,NULL);

         for (x=0; x < ret; ++x) {
            if ( (last = dmaRet[x]) > 0.0 ) {
               rate += 1.0;
               bw += (last * 8.0);
            }
         }

         if ( ret > 0 ) dmaRetIndexes(s,ret,dmaIndex);

	 if ( total == 0 ) {
            if ( ret > max ) max = ret;
            if ( ret < min ) min = ret;
	 }
	 total += ret;
      }

      gettimeofday(&eTime,NULL);

      timersub(&eTime,&sTime,&dTime);
      duration = dTime.tv_sec + (float)dTime.tv_usec/1000000.0;

      rate = rate / duration;
      bw   = bw   / duration;

      printf("%8i   %8i    %1.3e   %8i   %1.2e   %1.2e   %1.2e\n",min,max,last,args.count,duration,rate,bw);
      rate = 0.0;
      bw   = 0.0;
   }

   return(0);
}

