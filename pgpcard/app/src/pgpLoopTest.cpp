/**
 *-----------------------------------------------------------------------------
 * Title      : PGP rate test utility
 * ----------------------------------------------------------------------------
 * File       : pgpLoopTest.cpp
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * Utility to rate test the PGP card. This utility will create a set number of
 * write and read threads to emulate a number of read and write applications.
 * ----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to 
 * the license terms in the LICENSE.txt file found in the top-level directory 
 * of this distribution and at: 
    * https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
 * No part of the aes_stream_drivers package, including this file, may be 
 * copied, modified, propagated, or distributed except according to the terms 
 * contained in the LICENSE.txt file.
 * ----------------------------------------------------------------------------
**/

#include <sys/types.h>
#include <linux/types.h>
#include <unistd.h>
#include <stdio.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <argp.h>
#include <pthread.h>
#include <PgpDriver.h>
#include <PrbsData.h>
using namespace std;

const  char * argp_program_version = "pgpLoopTest 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

struct PrgArgs {
   const char * path;
   uint32_t     vcMask;
   uint32_t     prbsDis;
   uint32_t     size;
   uint32_t     idxEn;
   uint32_t     pause;
};

static struct PrgArgs DefArgs = { "/dev/pgpcard_0", 0xFFFFFFFF, 0, 10000, 0 };

static char   args_doc[] = "";
static char   doc[]      = "";

static struct argp_option options[] = {
   { "path",    'p', "PATH",   OPTION_ARG_OPTIONAL, "Path of pgpcard device to use. Default=/dev/pgpcard_0.",0},
   { "vcmask",  'v', "MASK",   OPTION_ARG_OPTIONAL, "Mask of vcs for test. 1 bit per vc in hex. i.e. 0xFF.",0},
   { "prbsdis", 'd', 0,        OPTION_ARG_OPTIONAL, "Disable PRBS checking.",0},
   { "size",    's', "SIZE",   OPTION_ARG_OPTIONAL, "Size for transmitted frames.",0},
   { "indexen", 'i', 0,        OPTION_ARG_OPTIONAL, "Use index based receive buffers.",0},
   { "time",    't', "TIME",   OPTION_ARG_OPTIONAL, "Pause time between writes in uSec. Default=0",0},
   {0}
};

error_t parseArgs ( int key,  char *arg, struct argp_state *state ) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch(key) {
      case 'p': args->path    = arg; break;
      case 'v': args->vcMask  = strtol(arg,NULL,16); break;
      case 's': args->size    = strtol(arg,NULL,10); break;
      case 'd': args->prbsDis = 1; break;
      case 'i': args->idxEn   = 1; break;
      case 't': args->pause   = strtol(arg,NULL,10); break;
      default: return ARGP_ERR_UNKNOWN; break;
   }
   return(0);
}

static struct argp argp = {options,parseArgs,args_doc,doc};

class RunData {
   public:
      const char * dev;
      uint32_t     size;
      uint32_t     lane;
      uint32_t     vc;
      uint64_t     count;
      uint64_t     total;
      uint64_t     prbErr;
      uint32_t     pause;
      char         id[10];
      bool         idxEn;
      bool         enable;
      bool         prbEn;
      bool         running;
};

void *runWrite ( void *t ) {
   fd_set          fds;
   struct timeval  timeout;
   int32_t         ret;
   void *          data;
   uint32_t        lane;
   uint32_t        vc;
   int32_t         fd;
   PrbsData        prbs(32,4,1,2,6,31);
   void **         dmaBuffers;
   uint32_t        dmaSize;
   uint32_t        dmaCount;
   int32_t         dmaIndex;
   bool            prbValid;

   RunData *txData = (RunData *)t;

   if ( (fd = open(txData->dev, O_RDWR )) < 0 ) {
      printf("Error opening device\n");
      txData->running = false;
      return NULL;
   }

   if ( txData->idxEn ) {
      if ((dmaBuffers = dmaMapDma(fd,&dmaCount,&dmaSize)) == NULL ) {
         printf("Write failed to map dma buffer\n");
         txData->running = false;
         return(NULL);
      }
   }
   else {
      if ((data = malloc(txData->size)) == NULL ) {
         printf("Write failed to allocate buffer\n");
         txData->running = false;
         return(NULL);
      }
   }

   lane  = txData->lane;
   vc    = txData->vc;

   prbValid = false;

   usleep(1000000+100*(lane*4+vc));
   printf("Starting write thread. Lane=%i, Vc=%i, Size=%i\n",lane,vc,txData->size);

   while (txData->enable) {

      // Setup fds for select call
      FD_ZERO(&fds);
      FD_SET(fd,&fds);

      // Wait for write ready
      timeout.tv_sec=0;
      timeout.tv_usec=100;
      ret = select(fd+1,NULL,&fds,NULL,&timeout);
      if ( ret != 0 ) {

         if ( txData->idxEn ) {
            dmaIndex = dmaGetIndex(fd);
            if ( dmaIndex < 0 ) continue;
            data = dmaBuffers[dmaIndex];
         }

         // Gen data
         if ( txData->prbEn && ! prbValid ) {
            prbs.genData(data,txData->size);
            prbValid = true;
         }

         if ( txData->idxEn ) ret = dmaWriteIndex(fd,dmaIndex,txData->size,0,pgpSetDest(lane,vc));
         else ret = dmaWrite(fd,data,txData->size,0,pgpSetDest(lane,vc));

         if ( ret < 0 ) {
            printf("Write Error at count %lu. Lane=%i, VC=%i\n",txData->count,lane,vc);
            break;
         }
         else if ( ret > 0 ) {
            txData->count++;
            txData->total += ret;
            prbValid = false;
            if ( txData->pause > 0 ) usleep(txData->pause);
         }
      }
   }

   if ( txData->idxEn ) dmaUnMapDma(fd,dmaBuffers);
   else free(data);
   close(fd);

   txData->running = false;

   printf("Write thread stopped!. Lane=%i, VC=%i\n",lane,vc);

   pthread_exit(NULL);
   return(NULL);
}


void *runRead ( void *t ) {
   fd_set          fds;
   struct timeval  timeout;
   int32_t         ret;
   void *          data;
   uint32_t        maxSize;
   uint32_t        lane;
   uint32_t        vc;
   uint32_t        rxLane;
   uint32_t        rxVc;
   uint32_t        rxErr;
   uint32_t        rxDest;
   uint64_t        mask;
   int32_t         fd;
   void **         dmaBuffers;
   uint32_t        dmaSize;
   uint32_t        dmaCount;
   uint32_t        dmaIndex;
   bool            idxEn;

   PrbsData        prbs(32,4,1,2,6,31);

   RunData *rxData = (RunData *)t;

   maxSize = rxData->size*2;
   idxEn   = rxData->idxEn;


   if ( (fd = open(rxData->dev, O_RDWR )) < 0 ) {
      printf("Error opening device\n");
      rxData->running = false;
      return NULL;
   }

   if ( rxData->idxEn ) {
      if ((dmaBuffers = dmaMapDma(fd,&dmaCount,&dmaSize)) == NULL ) {
         printf("Read failed to map dma buffer\n");
         rxData->running = false;
         return(NULL);
      }
   }
   else {
      if ((data = malloc(maxSize)) == NULL ) {
         printf("Read failed to allocate buffer\n");
         rxData->running = false;
         return(NULL);
      }
   }

   lane = rxData->lane;
   vc   = rxData->vc;
   mask = (1 << (lane*4 + vc));

   usleep(100*(lane*4+vc));
   if ( dmaSetMask(fd,mask) != 0 ) {
      printf("Error setting mask. lane=%i, vc=%i, mask=0x%.8lx\n",lane,vc,mask);
      close(fd);
      return NULL;
   }

   printf("Starting read thread.  Lane=%i, Vc=%i, Size=%i\n",lane,vc,rxData->size);

   while (rxData->enable) {

      // Setup fds for select call
      FD_ZERO(&fds);
      FD_SET(fd,&fds);

      // Wait for read ready
      timeout.tv_sec=0;
      timeout.tv_usec=100;
      ret = select(fd+1,&fds,NULL,NULL,&timeout);
      if ( ret != 0 ) {

         if ( idxEn ) {
            ret = dmaReadIndex(fd,&dmaIndex,NULL,&rxErr,&rxDest);
            data = dmaBuffers[dmaIndex];
         }
         else ret = dmaRead(fd,data,maxSize,NULL,&rxErr,&rxDest);

         rxVc   = pgpGetVc(rxDest);
         rxLane = pgpGetLane(rxDest);

         if ( ret != 0 ) {
            
            //  data
            if ( (rxData->prbEn) && (! prbs.processData(data,ret)) ) {
               rxData->prbErr++;
               printf("Prbs mismatch. count=%lu, lane=%i, vc=%i\n",rxData->count,lane,vc);
            }
            if ( idxEn ) dmaRetIndex(fd,dmaIndex);

            // Stop on size mismatch or frame errors
            if ( ret != (int)rxData->size || rxErr != 0 || rxLane != lane || rxVc != vc) {
               printf("Read Error. Lane=%i, VC=%i, ExpLane=%i, ExpVc=%i, Ret=%i, Exp=%i, rxErr=%i\n",rxLane,rxVc,lane,vc,ret,rxData->size,rxErr);
               break;
            }
            else {
               rxData->count++;
               rxData->total += ret;
            }
         }
      }
   }

   if ( idxEn ) dmaUnMapDma(fd,dmaBuffers);
   else free(data);

   close(fd);
   rxData->running = false;

   printf("Read thread stopped!.  Lane=%i, VC=%i\n",lane,vc);

   pthread_exit(NULL);
   return(NULL);
}


int main (int argc, char **argv) {
   RunData     * txData[32];
   RunData     * rxData[32];
   pthread_t     txThread[32];
   pthread_t     rxThread[32];
   uint          x;
   time_t        c_tme;
   time_t        l_tme;
   uint          vcCount;
   uint          lastRx[32];
   uint          lastTx[32];
   double        totRxRate;
   uint64_t      totRx;
   uint64_t      totTx;
   uint64_t      totRxFreq;
   uint64_t      totPrb;
   double        rxRate;
   bool          runEn;
   bool          allDone;
   int32_t       s;

   struct PrgArgs args;
   struct PgpInfo info;

   memcpy(&args,&DefArgs,sizeof(struct PrgArgs));
   argp_parse(&argp,argc,argv,0,0,&args);

   if ( (s = open(args.path, O_RDWR)) <= 0 ) {
      printf("Error opening %s\n",args.path);
      return(1);
   }
   pgpGetInfo(s,&info);

   // Generating read threads
   vcCount = 0;
   for (x=0; x < 32; x++) {
      if ( ((1 << x) & args.vcMask) && ((1 << (x/4)) & info.laneMask) && ((1 << (x%4)) & info.vcPerMask)) {
         rxData[vcCount] = new RunData;
         txData[vcCount] = new RunData;

         memset(rxData[vcCount],0,sizeof(RunData));

         rxData[vcCount]->enable  = true;
         rxData[vcCount]->running = true;
         rxData[vcCount]->lane    = x/4;
         rxData[vcCount]->vc      = x % 4;
         rxData[vcCount]->size    = (args.size + (x*4)); // (lane * 4 + vc) * 4
         rxData[vcCount]->dev     = args.path;
         rxData[vcCount]->idxEn   = args.idxEn;
         rxData[vcCount]->prbEn   = !args.prbsDis;
         rxData[vcCount]->pause   = args.pause;

         sprintf(rxData[vcCount]->id,"%i-%i",x/4,x%4);
         memcpy(txData[vcCount],rxData[vcCount],sizeof(RunData));

         if ( pthread_create(&rxThread[vcCount],NULL,runRead,rxData[vcCount]) ) {
            printf("Error creating read thread\n");
            return(2);
         }

         if ( pthread_create(&txThread[vcCount],NULL,runWrite,txData[vcCount]) ) {
            printf("Error creating write thread\n");
            return(2);
         }
         vcCount++;
      }
   }
   time(&c_tme);    
   time(&l_tme);    

   usleep(15000);
   runEn = true;
   allDone = false;
   while (!allDone) {
      sleep(1);

      // Check for stopped threads
      allDone = true;
      for (x=0; x < vcCount; x++) {
         if ( txData[x]->running == false ) runEn = false; else allDone = false;
         if ( rxData[x]->running == false ) runEn = false; else allDone = false;
      }
      if ( runEn == false ) {
         for (x=0; x < vcCount; x++) {
            txData[x]->enable = 0;
            rxData[x]->enable = 0;
         }
      }

      time(&c_tme);
      printf("\n\n");

      printf("Lane-VC:");
      for (x=0; x < vcCount; x++) printf(" %15s",txData[x]->id);
      printf("\nTxCount:");
      for (x=0; x < vcCount; x++) printf(" %15lu",txData[x]->count);
      printf("\n TxFreq:");
      for (x=0; x < vcCount; x++) printf(" %15lu",txData[x]->count-lastTx[x]);
      printf("\nTxBytes:");
      for (x=0; x < vcCount; x++) printf(" %15lu",txData[x]->total);
      printf("\n TxRate:");

      totTx = 0;
      for (x=0; x < vcCount; x++) {
         printf(" %15e",((double)(txData[x]->count-lastTx[x]) * 8.0 * (double)args.size) / (double)(c_tme-l_tme));
         lastTx[x] = txData[x]->count;
         totTx    += txData[x]->count;
      }
      printf("\n");

      printf("RxCount:");
      for (x=0; x < vcCount; x++) printf(" %15lu",rxData[x]->count);
      printf("\n RxFreq:");
      for (x=0; x < vcCount; x++) printf(" %15lu",rxData[x]->count-lastRx[x]);
      printf("\nRxBytes:");
      for (x=0; x < vcCount; x++) printf(" %15lu",rxData[x]->total);

      if ( ! args.prbsDis ) {
         printf("\n PrbErr:");
         for (x=0; x < vcCount; x++) printf(" %15lu",rxData[x]->prbErr);
      }
      printf("\n RxRate:");

      totRxRate = 0;
      totRxFreq = 0;
      totRx     = 0;
      totPrb    = 0;
      for (x=0; x < vcCount; x++) {
         rxRate = ((double)(rxData[x]->count-lastRx[x]) * 8.0 * (double)args.size) / (double)(c_tme-l_tme);
         printf(" %15e",rxRate);
         totRxFreq += (rxData[x]->count-lastRx[x]);
         lastRx[x] = rxData[x]->count;
         totRx     += rxData[x]->count;
         totPrb    += rxData[x]->prbErr;
         totRxRate += rxRate;
      }
      printf("\n");
      printf("  TotTx: %15lu\n",totTx);
      printf("  TotRx: %15lu\n",totRx);
      printf("TotFreq: %15lu\n",totRxFreq);
      if ( ! args.prbsDis ) printf(" PrbErr: %15lu\n",totPrb);
      printf("TotRate: %15e\n",totRxRate);
      l_tme = c_tme;
   }

   printf("\nMain thread stopped!.\n");

   // Wait for thread to stop
   for (x=0; x < vcCount; x++) {
      pthread_join(txThread[x], NULL);
      pthread_join(rxThread[x], NULL);
   }

   for (x=0; x < vcCount; x++) {
      delete txData[x];
      delete rxData[x];
   }
   close(s);
   return(0);
}

