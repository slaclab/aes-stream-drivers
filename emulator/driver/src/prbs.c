/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Kernel-C port of the userspace PRBS test pattern generator/verifier.
 *    Mirrors common/app_lib/PrbsData.cpp's default 32-bit / 4-tap {1,2,6,31}
 *    configuration so the emulator can produce and consume the same
 *    byte-exact frame format the userspace loopback test expects.
 *
 *    flfsr(), gen_data() and process_data() share the same tap table and
 *    the same LFSR helper so the generator and the verifier cannot drift.
 *    Size-violation paths hard-fail via WARN_ON_ONCE() + -EINVAL instead
 *    of the userspace silent-recover branch (kernel-safety delta).
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

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/export.h>

#include "prbs.h"

/* Tap positions for the default PrbsData(32, 4, 1, 2, 6, 31) configuration.
 * Kept as a compile-time constant so the compiler can unroll the inner XOR
 * loop while still preserving the exact computation shape of
 * PrbsData::flfsr() (cross-validated byte-for-byte by
 * emulator/driver/tests/prbs_cross_validate_cxx). */
static const u8 emu_prbs_taps[EMU_PRBS_TAP_COUNT] = { 1, 2, 6, 31 };

u32 emu_prbs_flfsr(u32 input)
{
   u32 bit = 0;
   u32 x;

   /* Accumulate feedback bit from the tapped positions. Matches
    * PrbsData::flfsr() exactly -- do not fold to __builtin_parity or a
    * hardware intrinsic; the 1M-iteration cross-validation harness
    * relies on the iteration shape being identical to the userspace
    * reference. */
   for (x = 0; x < EMU_PRBS_TAP_COUNT; x++)
      bit ^= (input >> emu_prbs_taps[x]) & 1u;

   return (input << 1) | bit;
}
EXPORT_SYMBOL_GPL(emu_prbs_flfsr);

int emu_prbs_gen_data(void *buf, size_t size, u32 sequence)
{
   u32 *data32;
   u32 value;
   size_t word;
   size_t words;

   if (!buf)
      return -EINVAL;

   /* Size guard: hard-fail via WARN_ON_ONCE instead of PrbsData.cpp:90's
    * silent `return;` branch (GPU-05 behavior delta). Mitigates T-01-01:
    * an under-sized or unaligned buffer would otherwise overflow the
    * word-indexed write loop below. */
   if (size < EMU_PRBS_MIN_BYTES || (size & 0x3u)) {
      WARN_ON_ONCE(1);
      return -EINVAL;
   }

   data32 = (u32 *)buf;

   data32[0] = sequence;
   data32[1] = (u32)((size - 4u) / 4u);

   value = sequence;
   words = size / sizeof(u32);
   for (word = 2; word < words; word++) {
      value = emu_prbs_flfsr(value);
      data32[word] = value;
   }

   return (int)(sequence + 1u);
}
EXPORT_SYMBOL_GPL(emu_prbs_gen_data);

int emu_prbs_process_data(const void *buf, size_t size)
{
   const u32 *data32;
   u32 expected;
   u32 event_length;
   u32 got;
   u32 value;
   size_t word;
   size_t words;

   if (!buf)
      return -EINVAL;

   /* Size guard: hard-fail via WARN_ON_ONCE instead of PrbsData.cpp:145's
    * silent-recover branch (GPU-05 behavior delta). Mitigates T-01-02:
    * an under-sized or unaligned caller buffer would otherwise read past
    * the caller-owned region in the word loop below. */
   if (size < EMU_PRBS_MIN_BYTES || (size & 0x3u)) {
      WARN_ON_ONCE(1);
      return -EINVAL;
   }

   data32 = (const u32 *)buf;

   expected = data32[0];
   event_length = (data32[1] * 4u) + 4u;

   /* Frame-length header must agree with the caller-supplied size,
    * otherwise the payload loop bound is untrustworthy (T-01-02). */
   if (event_length != size) {
      WARN_ON_ONCE(1);
      return -EINVAL;
   }

   /* Stateless verifier: no _sequence mutation. gpu_engine tracks its
    * own generator counter for the RX direction (rx_prbs_seq); the TX
    * verifier extracts the expected sequence from the frame header
    * (data32[0]) and so needs no engine-side state. Mismatches return
    * -EILSEQ so the caller can bump a counter and pr_info_ratelimited()
    * rather than emitting userspace-style fprintf noise from kernel
    * context. */
   value = expected;
   words = size / sizeof(u32);
   for (word = 2; word < words; word++) {
      value = emu_prbs_flfsr(value);
      got = data32[word];
      if (got != value)
         return -EILSEQ;
   }
   return 0;
}
EXPORT_SYMBOL_GPL(emu_prbs_process_data);
