/**
 *-----------------------------------------------------------------------------
 * Description:
 * This program will open up a AXIS DMA port and attempt to write data.
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
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <argp.h>
#include <DmaDriver.h>
using namespace std;

int main (int argc, char **argv) {
   int32_t       s;
   int32_t       ret;
   uint32_t      count;
   void *        txData=NULL;
   uint32_t      dmaSize;
   uint32_t      dmaCount;
   struct timeval startTime;
   struct timeval endTime;
   struct timeval diffTime;

   //dmaSize = 2097150;
   //dmaSize = 655360*10;
   //dmaSize = 655360*2;
   //dmaSize = 655360;
   //dmaSize = 655360/2;
   //dmaSize = 655360/10;
   dmaSize = 100;
   dmaCount = 10000;


   if ( (s = open("/dev/datadev_0", O_RDWR)) <= 0 ) {
      printf("Error opening\n");
      return(1);
   }

   if ((txData = malloc(dmaSize)) == NULL ) {
      printf("Failed to allocate rxData!\n");
      return(0);
   }
   count = 0;

   gettimeofday(&startTime, NULL);
   do {

      while ( (ret = dmaWrite(s,txData,dmaSize,0,0) ) == 0 );

      if ( ret < 0 ) {
         printf("Got write error\n");
         return(0);
      }

   } while ( ++count < dmaCount );
   gettimeofday(&endTime, NULL);

   free(txData);

   timersub(&endTime, &startTime, &diffTime);

   float duration = (float)diffTime.tv_sec + (float)diffTime.tv_usec / 1000000.0;
   float rate = count / duration;
   float period = 1.0 / rate;
   float bw = ((float)dmaCount * (float)dmaSize) / duration;

   printf("Wrote %i events of size %i in %f seconds, rate = %f hz, period = %f s, bw = %e B/s\n",dmaCount, dmaSize, duration, rate, period, bw);

   close(s);
   return(0);
}

