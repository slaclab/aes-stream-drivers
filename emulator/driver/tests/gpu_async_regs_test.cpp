/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Compile-time regression test for the version-selecting offset macros in
 *    include/GpuAsyncRegs.h.
 *
 *    GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET() used to select the V4 layout
 *    with (_ver == 4), so GpuAsyncCore version 5 fell through to the V1
 *    base (0x400) instead of 0x3000. The sibling READ_ADDR macros select
 *    with (_ver < 4). Every static_assert below pins the V1 layout to
 *    versions below 4 and the V4 layout to versions 4 and above.
 *
 *    Exit codes: 0=PASS. A regression fails at compile time.
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

#include "GpuAsyncRegs.h"

// Remote read size: V1 base 0x400 (stride 4), V4 base 0x3000 (stride 4).
static_assert(GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET(3, 0) == 0x0400, "v3 idx0 must use V1 layout");
static_assert(GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET(3, 2) == 0x0408, "v3 idx2 must use V1 layout");
static_assert(GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET(4, 0) == 0x3000, "v4 idx0 must use V4 layout");
static_assert(GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET(4, 2) == 0x3008, "v4 idx2 must use V4 layout");
static_assert(GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET(5, 0) == 0x3000, "v5 idx0 must use V4 layout");
static_assert(GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET(5, 2) == 0x3008, "v5 idx2 must use V4 layout");

// Sibling macros already select with (_version < 4); pin them so all three
// stay in agreement.
static_assert(GPU_ASYNC_REG_READ_ADDR_L_OFFSET(3, 1) == 0x0210, "v3 read addr L must use V1 layout");
static_assert(GPU_ASYNC_REG_READ_ADDR_H_OFFSET(3, 1) == 0x0214, "v3 read addr H must use V1 layout");
static_assert(GPU_ASYNC_REG_READ_ADDR_L_OFFSET(4, 1) == 0x6008, "v4 read addr L must use V4 layout");
static_assert(GPU_ASYNC_REG_READ_ADDR_H_OFFSET(4, 1) == 0x600C, "v4 read addr H must use V4 layout");
static_assert(GPU_ASYNC_REG_READ_ADDR_L_OFFSET(5, 1) == 0x6008, "v5 read addr L must use V4 layout");
static_assert(GPU_ASYNC_REG_READ_ADDR_H_OFFSET(5, 1) == 0x600C, "v5 read addr H must use V4 layout");

int main(void) {
   printf("gpu_async_regs_test: PASS (REMOTE_READ_SIZE v3=0x%04x v4=0x%04x v5=0x%04x)\n",
          GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET(3, 0),
          GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET(4, 0),
          GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET(5, 0));
   return 0;
}
