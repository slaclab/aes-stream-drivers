/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Userspace shim for <linux/types.h>. Provides the minimal kernel-type
 *    aliases that emulator/driver/src/prbs.{c,h} consume so the same
 *    source can be compiled as plain userspace C in the cross-validation
 *    harness (emulator/driver/tests/). Included by way of the -I path
 *    added in emulator/driver/tests/Makefile, shadowing any system
 *    <linux/types.h> during the test build only.
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
#ifndef _LINUX_TYPES_H
#define _LINUX_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#endif  /* _LINUX_TYPES_H */
