/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Kernel-C port of the userspace PRBS test pattern generator/verifier
 *    (common/app_lib/PrbsData.cpp, default constructor:
 *    PrbsData(32, 4, 1, 2, 6, 31)).
 *
 *    Shares a single flfsr() helper between the generator (gen_data)
 *    and the stateless verifier (process_data) so the two cannot drift.
 *    Byte-level equivalence with the userspace reference is required
 *    for the closed-loop CI fixture and is cross-validated against
 *    PrbsData by the emulator/driver/tests/prbs_cross_validate_cxx harness.
 *
 *    Size-violation paths hard-fail with WARN_ON_ONCE() + -EINVAL
 *    (kernel-safe delta vs. PrbsData's silent return;).
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
#ifndef __EMU_PRBS_H__
#define __EMU_PRBS_H__

#include <linux/types.h>

/* LFSR taps: polynomial x^32 + x^31 + x^6 + x^2 + 1 (matches
 * PrbsData(32, 4, 1, 2, 6, 31) in common/app_lib/PrbsData.cpp). */
#define EMU_PRBS_WIDTH      32
#define EMU_PRBS_TAP_COUNT  4
#define EMU_PRBS_MIN_BYTES  12      /* 3 u32 words: sequence + length + >=1 payload */

/* One-step LFSR advance. Must match PrbsData::flfsr() exactly. */
u32 emu_prbs_flfsr(u32 input);

/* Fill `buf` (size bytes, must be 4-byte aligned, >= 12) with
 *   [sequence][(size-4)/4][flfsr(sequence)][flfsr(flfsr(sequence))]...
 * Returns the next sequence value (sequence + 1) or -EINVAL on bad size.
 * Callers track the sequence across frames. */
int emu_prbs_gen_data(void *buf, size_t size, u32 sequence);

/* Verify `buf` (size bytes) against the PRBS protocol.
 * Returns 0 on match, -EINVAL on size violation (WARN_ON_ONCE),
 * -EILSEQ on payload mismatch. */
int emu_prbs_process_data(const void *buf, size_t size);

#endif /* __EMU_PRBS_H__ */
