/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Userspace regression test for dmaAddMaskBytes() in include/DmaDriver.h.
 *
 *    dmaAddMaskBytes used to set the destination bit with "+=" instead of
 *    "|=". That is only correct the first time a destination is added:
 *    adding the same destination twice carries into the next bit, so
 *    dest 0 twice left mask[0] = 0x02 (dest 1 only) and dest 7 twice left
 *    mask[0] = 0x00 (nothing). Any caller building a mask from a list with
 *    a duplicate entry (e.g. dmaRead -m 0,0) silently selected the wrong
 *    destinations.
 *
 *    The test adds dest 0 and dest 7 twice each and asserts that mask[0]
 *    is exactly 0x81 with every other byte still zero, then checks that
 *    repeated adds across byte boundaries and an out-of-range destination
 *    leave the mask unchanged. No kernel module or device node is required.
 *
 *    Exit codes: 0=PASS, 1=mismatch.
 *----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the aes_stream_drivers package, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 *----------------------------------------------------------------------------
 **/

#include <cstdio>
#include <cstdint>
#include <cstring>

#include "DmaDriver.h"

// Returns the number of bytes in mask that differ from expected and prints
// a diagnostic for each one.
static int checkMask(const char* name, const uint8_t* mask, const uint8_t* expected) {
   int      failures = 0;
   uint32_t i;

   for (i = 0; i < DMA_MASK_SIZE; i++) {
      if (mask[i] != expected[i]) {
         fprintf(stderr, "dma_addmask_test: FAIL %s: mask[%u]=0x%02x expected 0x%02x\n",
                 name, i, mask[i], expected[i]);
         failures++;
      }
   }
   return failures;
}

int main(void) {
   int     failures = 0;
   uint8_t mask[DMA_MASK_SIZE];
   uint8_t expected[DMA_MASK_SIZE];

   // Duplicate destinations within one byte: dest 0 twice and dest 7 twice
   // must leave exactly bits 0 and 7 set, with no carry into neighbours.
   dmaInitMaskBytes(mask);
   dmaAddMaskBytes(mask, 0);
   dmaAddMaskBytes(mask, 0);
   dmaAddMaskBytes(mask, 7);
   dmaAddMaskBytes(mask, 7);
   memset(expected, 0, sizeof(expected));
   expected[0] = 0x81;
   failures += checkMask("dest 0,0,7,7", mask, expected);

   // Duplicate destinations in a later byte: dest 15 twice must not carry
   // out of mask[1] into mask[2].
   dmaInitMaskBytes(mask);
   dmaAddMaskBytes(mask, 15);
   dmaAddMaskBytes(mask, 15);
   dmaAddMaskBytes(mask, 8);
   memset(expected, 0, sizeof(expected));
   expected[1] = 0x81;
   failures += checkMask("dest 15,15,8", mask, expected);

   // Adding every destination of a byte, then all of them again, must
   // saturate at 0xff rather than wrap to 0x00.
   dmaInitMaskBytes(mask);
   for (uint32_t pass = 0; pass < 2; pass++) {
      for (uint32_t dest = 16; dest < 24; dest++) {
         dmaAddMaskBytes(mask, dest);
      }
   }
   memset(expected, 0, sizeof(expected));
   expected[2] = 0xff;
   failures += checkMask("dest 16..23 twice", mask, expected);

   // Out-of-range destination must be ignored.
   dmaInitMaskBytes(mask);
   dmaAddMaskBytes(mask, 8 * DMA_MASK_SIZE);
   memset(expected, 0, sizeof(expected));
   failures += checkMask("dest out of range", mask, expected);

   if (failures) {
      fprintf(stderr, "dma_addmask_test: FAIL (%d mismatches)\n", failures);
      return 1;
   }

   printf("dma_addmask_test: PASS (dest 0,0,7,7 -> mask[0]=0x81)\n");
   return 0;
}
