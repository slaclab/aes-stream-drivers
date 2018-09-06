/**
 *-----------------------------------------------------------------------------
 * Title      : DMA read utility
 * ----------------------------------------------------------------------------
 * File       : dmaRead.cpp
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

const  char * argp_program_version = "pgpRead 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

struct PrgArgs {
   const char * path;
};

static struct PrgArgs DefArgs = { "/dev/axi_stream_dma_0" };

static char   args_doc[] = "";
static char   doc[]      = "";

static struct argp_option options[] = {
   { "path",    'p', "PATH",   OPTION_ARG_OPTIONAL, "Path of pgpcard device to use. Default=/dev/axi_stream_dma_0.",0},
   {0}
};

error_t parseArgs ( int key,  char *arg, struct argp_state *state ) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch(key) {
      case 'p': args->path = arg; break;
      default: return ARGP_ERR_UNKNOWN; break;
   }
   return(0);
}

static struct argp argp = {options,parseArgs,args_doc,doc};

int main (int argc, char **argv) {
   uint8_t       mask[DMA_MASK_SIZE];
   int32_t       s;
   int32_t       ret;
   int32_t       count;
   int32_t       rate;
   fd_set        fds;
   uint32_t      rxDest;
   uint32_t      rxFuser;
   uint32_t      rxLuser;
   uint32_t      rxFlags;
   PrbsData      prbs(32,4,1,2,6,31);
   bool          prbRes;
   void **       dmaBuffers;
   uint32_t      dmaSize;
   uint32_t      dmaCount;
   uint32_t      dmaIndex;
   time_t        lTime;
   time_t        cTime;

   struct PrgArgs args;

   struct timeval timeout;

   memcpy(&args,&DefArgs,sizeof(struct PrgArgs));
   argp_parse(&argp,argc,argv,0,0,&args);

   if ( (s = open(args.path, O_RDWR)) <= 0 ) {
      printf("Error opening %s\n",args.path);
      return(1);
   }

   dmaInitMaskBytes(mask);
   memset(mask,0xFF,DMA_MASK_SIZE);
   dmaSetMaskBytes(s,mask);

   if ( (dmaBuffers = dmaMapDma(s,&dmaCount,&dmaSize)) == NULL ) {
      printf("Failed to map dma buffers!\n");
      return(0);
   }

   rate   = 0;
   count  = 0;
   prbRes = 0;
   time(&lTime);
   do {

#if 0
      // Setup fds for select call
      FD_ZERO(&fds);
      FD_SET(s,&fds);

      // Setup select timeout for 1 second
      timeout.tv_sec=2;
      timeout.tv_usec=0;

      // Wait for Socket data ready
      ret = select(s+1,&fds,NULL,NULL,&timeout);
      if ( ret <= 0 ) {
         printf("Read timeout\n");
      }
      else {
#endif
         // DMA Read
         ret = dmaReadIndex(s,&dmaIndex,&rxFlags,NULL,&rxDest);

         rxFuser = axisGetFuser(rxFlags);
         rxLuser = axisGetFuser(rxFlags);

         if ( ret > 0 ) {
            dmaRetIndex(s,dmaIndex);

            count++;
            rate++;

	 }
         time(&cTime);
	 if ( cTime != lTime ) {
            printf("Read ret=%i, Dest=%i, Fuser=0x%.2x, Luser=0x%.2x, prbs=%i, count=%i, rate=%i\n",ret,rxDest,rxFuser,rxLuser,prbRes,count,rate);
	    rate = 0;
	    lTime = cTime;
	 }
      //}
   } while ( 1 );

   dmaUnMapDma(s,dmaBuffers);

   close(s);
   return(0);
}

