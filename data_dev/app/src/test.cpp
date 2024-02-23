/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    Opens an AXIS DMA port and attempts to write data using the DMA interface.
 *    Demonstrates the setup, execution, and timing of DMA write operations.
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

int main(int argc, char **argv) {
   int32_t s;
   int32_t ret;
   uint32_t count;
   void *txData = NULL;
   uint32_t dmaSize;
   uint32_t dmaCount;
   struct timeval startTime, endTime, diffTime;

   // Set DMA transaction size and count
   dmaSize = 100;      // Size of each DMA transaction in bytes
   dmaCount = 10000;   // Total number of DMA transactions to perform

   // Attempt to open the DMA device
   if ((s = open("/dev/datadev_0", O_RDWR)) <= 0) {
      printf("Error opening DMA device\n");
      return 1;
   }

   // Allocate memory for DMA data
   if ((txData = malloc(dmaSize)) == NULL) {
      printf("Failed to allocate memory for DMA data\n");
      return 0;
   }

   count = 0;

   // Get start time
   gettimeofday(&startTime, NULL);

   // Perform DMA write operations
   do {
      while ((ret = dmaWrite(s, txData, dmaSize, 0, 0)) == 0);

      if (ret < 0) {
         printf("DMA write error occurred\n");
         return 0;
      }
   } while (++count < dmaCount);

   // Get end time
   gettimeofday(&endTime, NULL);

   // Free allocated memory
   free(txData);

   // Calculate operation duration and performance metrics
   timersub(&endTime, &startTime, &diffTime);
   float duration = diffTime.tv_sec + diffTime.tv_usec / 1000000.0;
   float rate = count / duration;
   float period = 1.0 / rate;
   float bw = ((float)dmaCount * (float)dmaSize) / duration;

   // Display performance metrics
   printf("Wrote %u events of size %u in %.3f seconds, rate = %.3f Hz, period = %.3f s, bandwidth = %.3e B/s\n",
          dmaCount, dmaSize, duration, rate, period, bw);

   // Close the DMA device
   close(s);
   return 0;
}
