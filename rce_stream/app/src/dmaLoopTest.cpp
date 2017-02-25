/**
 *-----------------------------------------------------------------------------
 * Title      : DMA rate test utility
 * ----------------------------------------------------------------------------
 * File       : dmaLoopTest.cpp
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * Utility to rate test the DMA engine. This utility will create a set number of
 * write and read threads to emulate a number of read and write applications.
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
#include <AxisDriver.h>
#include <PrbsData.h>
using namespace std;

const  char * argp_program_version = "dmaLoopTest 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

struct PrgArgs {
   const char * path;
   const char * dest;
   uint32_t     prbsDis;
   uint32_t     size;
   uint32_t     idxEn;
   uint32_t     fuser;
   uint32_t     luser;
   uint32_t     pause;
   uint32_t     txDis;
};

static struct PrgArgs DefArgs = { "/dev/axi_stream_dma_0", "0", 0, 10000, 0,0x2,0x0,0,0 };

static char   args_doc[] = "";
static char   doc[]      = "";

static struct argp_option options[] = {
   { "path",    'p', "PATH",   OPTION_ARG_OPTIONAL, "Path of pgpcard device to use. Default=/dev/pgpcard_0.",0},
   { "dest",    'm', "LIST",   OPTION_ARG_OPTIONAL, "Comman seperated list of destinations.",0},
   { "prbsdis", 'd', 0,        OPTION_ARG_OPTIONAL, "Disable PRBS checking.",0},
   { "size",    's', "SIZE",   OPTION_ARG_OPTIONAL, "Size for transmitted frames.",0},
   { "indexen", 'i', 0,        OPTION_ARG_OPTIONAL, "Use index based receive buffers.",0},
   { "fuser",   'f', "FUSER",  OPTION_ARG_OPTIONAL, "Value for first user field in hex. Default=0x2",0},
   { "luser",   'l', "LUSER",  OPTION_ARG_OPTIONAL, "Value for last user field in hex. Default=0x0",0},
   { "time",    't', "TIME",   OPTION_ARG_OPTIONAL, "Pause time between writes in uSec. Default=0",0},
   { "txdis",   'r', "TIME",   OPTION_ARG_OPTIONAL, "Disable transmit threads. Default=0",0},
   {0}
};

error_t parseArgs ( int key,  char *arg, struct argp_state *state ) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch(key) {
      case 'p': args->path    = arg; break;
      case 'm': args->dest    = arg; break;
      case 's': args->size    = strtol(arg,NULL,10); break;
      case 'd': args->prbsDis = 1; break;
      case 'i': args->idxEn   = 1; break;
      case 'f': args->fuser   = strtol(arg,NULL,16); break;
      case 'l': args->luser   = strtol(arg,NULL,16); break;
      case 't': args->pause   = strtol(arg,NULL,10); break;
      case 'r': args->txDis   = strtol(arg,NULL,10); break;
      default: return ARGP_ERR_UNKNOWN; break;
   }
   return(0);
}

static struct argp argp = {options,parseArgs,args_doc,doc};

class RunData {
   public:
      const char * dev;
      uint32_t     size;
      uint32_t     dest;
      uint32_t     fuser;
      uint32_t     luser;
      uint32_t     count;
      uint32_t     total;
      uint32_t     prbErr;
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
      return(NULL);
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


   prbValid = false;

   usleep(1000000+100*(txData->dest));
   printf("Starting write thread. Dest=%i, Size=%i\n",txData->dest,txData->size);


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

         if ( txData->idxEn ) ret = axisWriteIndex(fd,dmaIndex,txData->size,txData->fuser,txData->luser,txData->dest);
         else ret = axisWrite(fd,data,txData->size,txData->fuser,txData->luser,txData->dest);

         if ( ret < 0 ) {
            printf("Write Error at count %lu. Dest=%i\n",txData->count,txData->dest);
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

   printf("Write thread stopped!. Dest=%i\n",txData->dest);

   pthread_exit(NULL);
   return(NULL);
}


void *runRead ( void *t ) {
   fd_set          fds;
   struct timeval  timeout;
   int32_t         ret;
   void *          data;
   uint32_t        maxSize;
   uint32_t        rxDest;
   uint32_t        rxFuser;
   uint32_t        rxLuser;
   int32_t         fd;
   void **         dmaBuffers;
   uint32_t        dmaSize;
   uint32_t        dmaCount;
   uint32_t        dmaIndex;
   bool            idxEn;
   uint8_t         mask[DMA_MASK_SIZE];

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
   
   dmaInitMaskBytes(mask);
   dmaAddMaskBytes(mask,rxData->dest);
   usleep(100*rxData->dest);

   if ( dmaSetMaskBytes(fd,mask) != 0 ) {
      printf("Error setting mask. Dest=%i\n",rxData->dest);
      rxData->running = false;
      close(fd);
      return NULL;
   }

   printf("Starting read thread.  Dest=%i, Size=%i\n",rxData->dest,rxData->size);

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
            ret = axisReadIndex(fd,&dmaIndex,&rxFuser,&rxLuser,&rxDest);
            data = dmaBuffers[dmaIndex];
         }
         else ret = axisRead(fd,data,maxSize,&rxFuser,&rxLuser,&rxDest);

         if ( ret != 0 ) {
            
            //  data
            if ( (rxData->prbEn) && (! prbs.processData(data,ret)) ) {
               rxData->prbErr++;
               printf("Prbs mismatch. count=%lu, dest=%i, index=%i\n",rxData->count,rxData->dest,dmaIndex);
            }
            if ( idxEn ) dmaRetIndex(fd,dmaIndex);

            // Stop on size mismatch or frame errors
            if ( ret != (int)rxData->size || rxDest != rxData->dest || rxData->fuser != rxFuser || rxData->luser != rxLuser) {
               printf("Read Error. Dest=%i, ExpDest=%i, Ret=%i, Exp=%i, Fuser=0x%.2x, Luser=0x%.2x\n",
                     rxDest,rxData->dest,ret,rxData->size,rxFuser,rxLuser);
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

   printf("Read thread stopped!.  Dest=%i\n",rxData->dest);

   pthread_exit(NULL);
   return(NULL);
}

int main (int argc, char **argv) {
   RunData     * txData[DMA_MASK_SIZE];
   RunData     * rxData[DMA_MASK_SIZE];
   pthread_t     txThread[DMA_MASK_SIZE];
   pthread_t     rxThread[DMA_MASK_SIZE];
   uint          x;
   time_t        c_tme;
   time_t        l_tme;
   uint          dCount;
   uint          lastRx[DMA_MASK_SIZE];
   uint          lastTx[DMA_MASK_SIZE];
   double        totRxRate;
   uint32_t      totRx;
   uint32_t      totRxFreq;
   uint32_t      totTx;
   uint32_t      totPrb;
   double        rxRate;
   bool          runEn;
   bool          allDone;
   char          tBuff[100];
   char *        tok;

   struct PrgArgs args;

   memcpy(&args,&DefArgs,sizeof(struct PrgArgs));
   argp_parse(&argp,argc,argv,0,0,&args);

   // Generating endpoints
   dCount = 0;

   strcpy(tBuff,args.dest);
   tok = strtok(tBuff,",");
   while ( tok != NULL ) {
      x = strtoul(tok,NULL,10);
      printf("Creating loop for dest %i\n",x);
      rxData[dCount] = new RunData;
      txData[dCount] = new RunData;

      memset(rxData[dCount],0,sizeof(RunData));

      rxData[dCount]->enable  = true;
      rxData[dCount]->running = true;
      rxData[dCount]->dest    = x;
      rxData[dCount]->fuser   = args.fuser;
      rxData[dCount]->luser   = args.luser;
      rxData[dCount]->size    = (args.size + (x*4)); // (lane * 4 + vc) * 4
      rxData[dCount]->dev     = args.path;
      rxData[dCount]->idxEn   = args.idxEn;
      rxData[dCount]->prbEn   = !args.prbsDis;
      rxData[dCount]->pause   = args.pause;

      sprintf(rxData[dCount]->id,"%i",x);
      memcpy(txData[dCount],rxData[dCount],sizeof(RunData));

      if ( pthread_create(&rxThread[dCount],NULL,runRead,rxData[dCount]) ) {
         printf("Error creating read thread\n");
         return(2);
      }

      if ( args.txDis == 0 ) {
         if ( pthread_create(&txThread[dCount],NULL,runWrite,txData[dCount]) ) {
            printf("Error creating write thread\n");
            return(2);
         }
      }
      else {
         txData[dCount]->running = false;
         txData[dCount]->enable  = false;
      }
      dCount++;
      tok = strtok(NULL,",");
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
      for (x=0; x < dCount; x++) {
         if ( args.txDis == 0 ) {
            if ( txData[x]->running == false ) runEn = false; else allDone = false;
         }
         if ( rxData[x]->running == false ) runEn = false; else allDone = false;
      }
      if ( runEn == false ) {
         for (x=0; x < dCount; x++) {
            txData[x]->enable = 0;
            rxData[x]->enable = 0;
         }
      }

      time(&c_tme);
      printf("\n\n");

      printf("   Dest:");
      for (x=0; x < dCount; x++) printf(" %15s",txData[x]->id);
      printf("\nTxCount:");
      for (x=0; x < dCount; x++) printf(" %15lu",txData[x]->count);
      printf("\n TxFreq:");
      for (x=0; x < dCount; x++) printf(" %15lu",txData[x]->count-lastTx[x]);
      printf("\nTxBytes:");
      for (x=0; x < dCount; x++) printf(" %15lu",txData[x]->total);
      printf("\n TxRate:");

      totTx = 0;
      for (x=0; x < dCount; x++) {
         printf(" %15e",((double)(txData[x]->count-lastTx[x]) * 8.0 * (double)args.size) / (double)(c_tme-l_tme));
         lastTx[x] = txData[x]->count;
         totTx    += txData[x]->count;
      }
      printf("\n");

      printf("RxCount:");
      for (x=0; x < dCount; x++) printf(" %15lu",rxData[x]->count);
      printf("\n RxFreq:");
      for (x=0; x < dCount; x++) printf(" %15lu",rxData[x]->count-lastRx[x]);
      printf("\nRxBytes:");
      for (x=0; x < dCount; x++) printf(" %15lu",rxData[x]->total);

      if ( ! args.prbsDis ) {
         printf("\n PrbErr:");
         for (x=0; x < dCount; x++) printf(" %15lu",rxData[x]->prbErr);
      }
      printf("\n RxRate:");

      totRxFreq = 0;
      totRxRate = 0;
      totRx     = 0;
      totPrb    = 0;
      for (x=0; x < dCount; x++) {
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
   for (x=0; x < dCount; x++) {
      if ( args.txDis == 0 ) pthread_join(txThread[x], NULL);
      pthread_join(rxThread[x], NULL);
   }

   for (x=0; x < dCount; x++) {
      delete txData[x];
      delete rxData[x];
   }

   return(0);
}

