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

// Dump bytes to stdout, in a neatly formatted manner
static inline void dumpBytes(void* buf, size_t count) {
   uint8_t* tmpbuf = (uint8_t*)buf;
   for (size_t i = 0; i < count; ++i) {
      printf("%02X ", tmpbuf[i]);
      if (i && (i+1) % 32 == 0)
         printf("\n");
   }
   printf("\n");
}

// Get current time in seconds since UNIX epoch, return as a double
static inline double curTime() {
   struct timespec tp;
   if (clock_gettime(CLOCK_MONOTONIC, &tp) < 0) {
      return 0;
   }
   return double(tp.tv_sec) + double(tp.tv_nsec) / 1e9;
}

#undef MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))

#undef MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))


#endif // _APPUTILS_H_