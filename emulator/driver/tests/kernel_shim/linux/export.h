/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Userspace shim for <linux/export.h>. Stubs EXPORT_SYMBOL_GPL to
 *    nothing so emulator/driver/src/prbs.c's symbol-export macros are
 *    no-ops when the file is compiled as part of the userspace
 *    cross-validation harness.
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
#ifndef _LINUX_EXPORT_H
#define _LINUX_EXPORT_H

#define EXPORT_SYMBOL_GPL(sym)  /* no-op in userspace harness */

#endif  /* _LINUX_EXPORT_H */
