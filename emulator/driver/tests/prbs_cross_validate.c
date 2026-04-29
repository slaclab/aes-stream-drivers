/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Userspace-compile wrapper around emulator/driver/src/prbs.c for the
 *    Phase 1 kernel-vs-userspace PRBS cross-validation harness.
 *
 *    The kernel source file under emulator/driver/src is included
 *    in-place (see the include directive below), so the binary tests
 *    the EXACT lines that datadev_emulator.ko ships -- any drift in
 *    prbs.c is caught at the next `make -C tests test`.
 *
 *    The Makefile puts emulator/driver/tests/kernel_shim/ on the -I path
 *    BEFORE the system include dirs, so the linux/ includes inside
 *    prbs.{c,h} resolve to the minimal userspace stubs under
 *    tests/kernel_shim/linux/ instead of the real kernel headers (which
 *    are unavailable from userspace for linux/bug.h and
 *    linux/export.h, and would drag in far too much for the others).
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

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Compile the kernel module's PRBS source as plain userspace C. The
 * kernel_shim/ subdirectory (added to the -I path in the tests/Makefile)
 * stubs out the linux/ includes prbs.c pulls in; no other translation
 * is required because prbs.c only uses u8/u32, WARN_ON_ONCE,
 * EXPORT_SYMBOL_GPL, and errno constants. */
#include "../src/prbs.c"  // NOLINT(build/include) -- deliberate cross-compile of the kernel source for byte-equivalence testing

/* Thin extern-C surface consumed by prbs_cross_validate_cxx.cpp. Keeping
 * the kernel-C source itself free of C++ linkage annotations means we do
 * not have to edit prbs.c at all -- any future drift in prbs.c is caught
 * by the next build of this harness. */
uint32_t harness_emu_flfsr(uint32_t input) {
   return emu_prbs_flfsr((u32)input);
}

int harness_emu_gen(uint8_t *buf, size_t size, uint32_t sequence) {
   return emu_prbs_gen_data(buf, size, (u32)sequence);
}

int harness_emu_process(const uint8_t *buf, size_t size) {
   return emu_prbs_process_data(buf, size);
}
