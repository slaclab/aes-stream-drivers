/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Userspace regression test for pgpSetMask() in pgpcard/include/PgpDriver.h.
 *
 *    DMA_Set_Mask stores its ioctl argument as the first 32 bits of the
 *    destination bitmap (dma_common.c: ((uint32_t *)newMask)[0] = arg), so
 *    the argument must be (1 << dest), not dest itself. pgpSetMask used to
 *    pass the bare destination number lane*4+vc, which reserved the wrong
 *    destination (or none at all for lane 0 / VC 0).
 *
 *    The test replaces ioctl() with a recording shim via the preprocessor,
 *    calls pgpSetMask() for every (lane, vc) in the 8x4 PGP space, and
 *    asserts that the value passed to DMA_Set_Mask is exactly the single
 *    bit that pgpAddMaskBytes() sets for the same (lane, vc) in the byte
 *    array form. No kernel module or device node is required.
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

// Pull in the real libc declaration first so the include guard keeps the
// header's own <sys/ioctl.h> include from being renamed by the macro below.
#include <sys/ioctl.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdarg>

static int           shim_calls = 0;
static unsigned long shim_cmd   = 0;
static unsigned long shim_arg   = 0;

static int pgp_test_ioctl(int fd, unsigned long cmd, ...) {
   va_list ap;
   (void)fd;
   va_start(ap, cmd);
   shim_arg = va_arg(ap, unsigned long);
   va_end(ap);
   shim_cmd = cmd;
   shim_calls++;
   return 0;
}

// Every ioctl() call inside the inline helpers of DmaDriver.h / PgpDriver.h
// now resolves to the recording shim above.
#define ioctl pgp_test_ioctl
#include "PgpDriver.h"
#undef ioctl

static const uint32_t PGP_LANES = 8;
static const uint32_t PGP_VCS   = 4;

int main(void) {
   int      failures = 0;
   uint32_t lane;
   uint32_t vc;
   uint8_t  maskBytes[DMA_MASK_SIZE];
   uint32_t expected;

   for (lane = 0; lane < PGP_LANES; lane++) {
      for (vc = 0; vc < PGP_VCS; vc++) {
         // Reference: the byte-array form of the same single-destination mask.
         dmaInitMaskBytes(maskBytes);
         pgpAddMaskBytes(maskBytes, lane, vc);
         memcpy(&expected, maskBytes, sizeof(expected));

         shim_calls = 0;
         shim_cmd   = 0;
         shim_arg   = 0;
         pgpSetMask(0, lane, vc);

         if (shim_calls != 1 || shim_cmd != DMA_Set_Mask || (uint32_t)shim_arg != expected) {
            fprintf(stderr,
                    "pgp_setmask_test: FAIL lane=%u vc=%u: calls=%d cmd=0x%lx arg=0x%lx expected cmd=0x%x arg=0x%08x\n",
                    lane, vc, shim_calls, shim_cmd, shim_arg, DMA_Set_Mask, expected);
            failures++;
         }
      }
   }

   if (failures) {
      fprintf(stderr, "pgp_setmask_test: FAIL (%d mismatches)\n", failures);
      return 1;
   }

   printf("pgp_setmask_test: PASS (%u lane/vc pairs, lane 1 vc 0 -> 0x10)\n", PGP_LANES * PGP_VCS);
   return 0;
}
