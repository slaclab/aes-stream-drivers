/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Main for the Phase 1 kernel-vs-userspace PRBS cross-validation
 *    harness.  Closes phase success criterion #4 by proving, byte for
 *    byte, that:
 *
 *      (a) emu_prbs_flfsr matches an independent userspace LFSR
 *          (taps {1,2,6,31}) across 1,000,000 iterations at a known
 *          seed (0xCAFEBABE).
 *
 *      (b) emu_prbs_gen_data(buf, 4096, 0) matches PrbsData::genData
 *          (buf, 4096) frame-for-frame -- the canonical userspace
 *          reference from common/app_lib/PrbsData.cpp; PrbsData's
 *          default constructor leaves _sequence=0, so this comparison
 *          does not require private-field access.
 *
 *      (c) emu_prbs_gen_data(buf, 4096, seed) matches the
 *          [seed][length_words][flfsr...] frame layout for two
 *          additional non-zero seeds (0xCAFEBABE, 0xDEADBEEF) using
 *          the same userspace LFSR as (a).  This covers the seed
 *          space PrbsData cannot reach without private access.
 *
 *    Any divergence at any iteration/byte causes the binary to exit
 *    non-zero after printing the exact mismatch site.  The program
 *    exits 0 on full agreement.
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
#include <cstdlib>
#include <cerrno>
#include <vector>

#include "PrbsData.h"

extern "C" {
   uint32_t harness_emu_flfsr(uint32_t input);
   int      harness_emu_gen(uint8_t *buf, size_t size, uint32_t sequence);
   int      harness_emu_process(const uint8_t *buf, size_t size);
}

// Userspace LFSR reference with the same taps PrbsData's default ctor
// installs ({1, 2, 6, 31}).  This is algorithmically identical to
// PrbsData::flfsr() but reimplemented here because PrbsData::flfsr is
// private; the PrbsData::genData comparison below exercises the real
// library code, so this local copy only needs to be spec-equivalent.
static uint32_t userspace_flfsr(uint32_t input) {
   static const uint32_t taps[4] = {1, 2, 6, 31};
   uint32_t bit = 0;
   for (int i = 0; i < 4; i++)
      bit ^= (input >> taps[i]) & 1u;
   return (input << 1) | bit;
}

// 1M-iteration LFSR equivalence: the phase-success gate.
static int compare_flfsr_1M(uint32_t seed) {
   uint32_t k = seed;
   uint32_t u = seed;
   const uint64_t N = 1000000ULL;

   for (uint64_t i = 0; i < N; i++) {
      k = harness_emu_flfsr(k);
      u = userspace_flfsr(u);
      if (k != u) {
         fprintf(stderr,
            "flfsr mismatch at iter=%llu seed=0x%08x "
            "kernel=0x%08x userspace=0x%08x\n",
            static_cast<unsigned long long>(i),
            seed, k, u);
         return 1;
      }
   }
   printf("flfsr: %llu iterations agree (seed=0x%08x, final=0x%08x)\n",
          static_cast<unsigned long long>(N), seed, k);
   return 0;
}

// Direct PrbsData::genData vs emu_prbs_gen_data byte-for-byte check at
// seed=0 -- the only seed reachable without touching PrbsData's private
// _sequence field.  This is the canonical must_have truth #3 gate.
static int compare_frame_against_prbsdata(size_t size) {
   std::vector<uint8_t> kbuf(size, 0);
   std::vector<uint8_t> ubuf(size, 0);

   // emu_prbs_gen_data returns (int)(sequence + 1) on success or -EINVAL
   // (== -22) on a size violation.  `sequence + 1` can itself read as a
   // negative int32_t when sequence >= 0x7FFFFFFE, so only -EINVAL is a
   // real error signal.
   const int kret = harness_emu_gen(kbuf.data(), size, 0u);
   if (kret == -EINVAL) {
      fprintf(stderr, "kernel emu_prbs_gen_data returned -EINVAL at seed=0\n");
      return 1;
   }

   PrbsData pd;  // default ctor: width=32, taps={1,2,6,31}, _sequence=0
   pd.genData(ubuf.data(), static_cast<uint32_t>(size));

   if (std::memcmp(kbuf.data(), ubuf.data(), size) != 0) {
      for (size_t i = 0; i + 4 <= size; i += 4) {
         const uint32_t *k = reinterpret_cast<const uint32_t *>(&kbuf[i]);
         const uint32_t *u = reinterpret_cast<const uint32_t *>(&ubuf[i]);
         if (*k != *u) {
            fprintf(stderr,
               "frame mismatch (emu vs PrbsData::genData) at word=%zu "
               "byte=%zu: kernel=0x%08x userspace=0x%08x\n",
               i / 4, i, *k, *u);
            return 1;
         }
      }
      return 1;
   }
   printf("frame: %zu-byte frame agrees with PrbsData::genData (seed=0)\n",
          size);

   // Round-trip: feed the kernel-generated frame into the userspace
   // verifier.  If the layout has any subtle byte order / length-word
   // disagreement, processData catches it.  PrbsData's processData is
   // stateful (it advances _sequence); run it on a fresh instance.
   PrbsData pd_rx;
   if (!pd_rx.processData(kbuf.data(), static_cast<uint32_t>(size))) {
      fprintf(stderr,
         "PrbsData::processData rejected the kernel-generated frame "
         "(seed=0, size=%zu)\n", size);
      return 1;
   }
   printf("frame: PrbsData::processData accepts the kernel-generated "
          "frame (seed=0)\n");
   return 0;
}

// Arbitrary-seed frame-layout check using the userspace LFSR reference
// (PrbsData cannot be primed to a non-zero seed without private access).
// Covers truth #3's intent for seeds the library cannot reach.
static int compare_frame_at_seed(uint32_t seed, size_t size) {
   std::vector<uint8_t> kbuf(size, 0);
   std::vector<uint8_t> ubuf(size, 0);

   // See the note in compare_frame_against_prbsdata: only -EINVAL is a
   // real error signal; `(int)(sequence + 1u)` can otherwise appear
   // negative for large seeds.
   const int kret = harness_emu_gen(kbuf.data(), size, seed);
   if (kret == -EINVAL) {
      fprintf(stderr, "kernel emu_prbs_gen_data returned -EINVAL at "
              "seed=0x%08x\n", seed);
      return 1;
   }

   // Construct the reference frame using the same spec prbs.c implements:
   // [seed][length_words][flfsr(seed)][flfsr(flfsr(seed))]...
   uint32_t *u32buf = reinterpret_cast<uint32_t *>(ubuf.data());
   u32buf[0] = seed;
   u32buf[1] = static_cast<uint32_t>((size - 4u) / 4u);
   uint32_t val = seed;
   const size_t words = size / 4u;
   for (size_t w = 2; w < words; w++) {
      val = userspace_flfsr(val);
      u32buf[w] = val;
   }

   if (std::memcmp(kbuf.data(), ubuf.data(), size) != 0) {
      for (size_t i = 0; i + 4 <= size; i += 4) {
         const uint32_t *k = reinterpret_cast<const uint32_t *>(&kbuf[i]);
         const uint32_t *u = reinterpret_cast<const uint32_t *>(&ubuf[i]);
         if (*k != *u) {
            fprintf(stderr,
               "frame mismatch at seed=0x%08x word=%zu byte=%zu: "
               "kernel=0x%08x userspace=0x%08x\n",
               seed, i / 4, i, *k, *u);
            return 1;
         }
      }
      return 1;
   }
   printf("frame: %zu-byte frame agrees at seed=0x%08x\n", size, seed);

   // Kernel-round-trip: the same stateless verifier inside prbs.c
   // should accept the frame it just generated.  Catches any future
   // drift between gen_data and process_data in a single hop.
   const int rret = harness_emu_process(kbuf.data(), size);
   if (rret != 0) {
      fprintf(stderr,
         "emu_prbs_process_data rejected its own frame at seed=0x%08x "
         "(rc=%d)\n", seed, rret);
      return 1;
   }
   printf("frame: emu_prbs_process_data accepts its own frame "
          "(seed=0x%08x)\n", seed);
   return 0;
}

int main(int /*argc*/, char ** /*argv*/) {
   const uint32_t primary_seed = 0xCAFEBABEu;
   const size_t   frame_size   = 4096;

   if (compare_flfsr_1M(primary_seed) != 0)
      return 1;

   if (compare_frame_against_prbsdata(frame_size) != 0)
      return 1;

   if (compare_frame_at_seed(primary_seed, frame_size) != 0)
      return 1;

   if (compare_frame_at_seed(0xDEADBEEFu, frame_size) != 0)
      return 1;

   printf("ALL CROSS-VALIDATION CHECKS PASSED\n");
   return 0;
}
