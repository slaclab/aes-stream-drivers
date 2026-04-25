/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Userspace shim for <linux/bug.h>. Stubs WARN_ON_ONCE to a printf
 *    so emulator/driver/src/prbs.c's size-guard branches compile as
 *    userspace C in the cross-validation harness.
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
#ifndef _LINUX_BUG_H
#define _LINUX_BUG_H

#include <stdio.h>

#define WARN_ON_ONCE(cond)                                          \
   do {                                                             \
      if (cond)                                                     \
         fprintf(stderr, "WARN_ON_ONCE: %s at %s:%d\n",             \
                 #cond, __FILE__, __LINE__);                        \
   } while (0)

#endif  /* _LINUX_BUG_H */
