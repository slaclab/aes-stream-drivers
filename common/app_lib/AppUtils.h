/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description: Assorted utilities for app code
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

#ifndef _APPUTILS_H_
#define _APPUTILS_H_

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <inttypes.h>

/**
 * @brief Dump bytes to stdout, in a neatly formatted manner
 * @param buf Buffer to dump
 * @param count Number of bytes in the buffer
 */
static inline void dumpBytes(void* buf, size_t count) {
   uint8_t* tmpbuf = (uint8_t*)buf;
   for (size_t i = 0; i < count; ++i) {
      printf("%02X ", tmpbuf[i]);
      if (i && (i+1) % 32 == 0)
         printf("\n");
   }
   printf("\n");
}

/**
 * @brief Get current time in seconds since UNIX epoch, return as a double
 */
static inline double curTime() {
   struct timespec tp;
   if (clock_gettime(CLOCK_MONOTONIC, &tp) < 0) {
      return 0;
   }
   return double(tp.tv_sec) + double(tp.tv_nsec) / 1e9;
}

/**
 * @brief Print results in a nice manner
 * @param which the "type" of I/O. Usually Rx or Tx, but may be empty string too
 * @param count Number of events
 * @param totalbytes total number of bytes transferred
 * @param elapsed Number of seconds elapsed
 */
static inline void printResults(const char* which, int64_t count, uint64_t totalBytes, double elapsed) {
   printf("\n");
   printf("Total %s Events  : %" PRId64 "\n", which, count);
   printf("Total %s Bytes   : %" PRIu64 " (%.2f GB)\n", which, totalBytes, double(totalBytes) / 1e9);
   printf("%s Rate          : %.2f Hz (%.2f kHz)\n", which, double(count) / elapsed, double(count) / elapsed / 1024.);
   printf("%s Speed         : %.f B/s (%.2f MB/s)\n",
      which, double(totalBytes) / elapsed, double(totalBytes) / elapsed / 1e6);
   printf("Elapsed:         : %.2f seconds\n", elapsed);
}

/**
 * @brief Calculate average
 */
template<typename T>
static inline T updateAverage(const T& current, const T& newval, uint32_t samples)
{
   return (current * samples + newval) / (samples + 1);
}

#undef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#undef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))


#endif  // _APPUTILS_H_
