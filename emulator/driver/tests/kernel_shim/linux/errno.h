/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Userspace shim for <linux/errno.h>. Defines the kernel errno
 *    constants emulator/driver/src/prbs.c actually returns (EINVAL,
 *    EILSEQ) directly, WITHOUT chaining to libc's <errno.h>. The libc
 *    <errno.h> re-enters this header via <bits/errno.h>, so routing
 *    through libc creates a guarded re-inclusion cycle that leaves
 *    EINVAL undefined at the prbs.c call site.
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
#ifndef _LINUX_ERRNO_H
#define _LINUX_ERRNO_H

/* Kernel errno values used by emulator/driver/src/prbs.c. Numeric
 * values match include/uapi/asm-generic/errno-base.h and errno.h
 * in the Linux kernel -- exact numeric match is not required here
 * (userspace never reads errno from the harness), but the constants
 * must exist as compile-time negatable integers. */
#ifndef EINVAL
#define EINVAL 22
#endif

#ifndef EILSEQ
#define EILSEQ 84
#endif

#endif  /* _LINUX_ERRNO_H */
