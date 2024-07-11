/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    This class is designed for generating and processing Pseudo-Random Binary
 *    Sequence (PRBS) test data. It supports configurable sequence widths and tap
 *    counts for flexibility in testing different PRBS configurations.
 *-----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to the
 * license terms in the LICENSE.txt file found in the top-level directory of
 * this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the aes_stream_drivers package, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 *-----------------------------------------------------------------------------
 **/

#ifndef __PRBS_DATA_H__
#define __PRBS_DATA_H__
#include <stdint.h>

// Main class for PRBS data generation and processing
class PrbsData {
   // Private member variables
   uint32_t * _taps;      // Array of tap positions for the LFSR
   uint32_t   _tapCnt;    // Number of taps
   uint32_t   _width;     // Width of the sequence
   uint32_t   _sequence;  // Current sequence value

   // Linear feedback shift register function
   uint32_t flfsr(uint32_t input);

public:
   // Constructors and destructor
   PrbsData(uint32_t width, uint32_t tapCnt, ...);
   PrbsData();
   ~PrbsData();

   // Generates PRBS data
   void genData(const void *data, uint32_t size);

   // Processes received PRBS data to check for integrity
   bool processData(const void *data, uint32_t size);
};

#endif  // __PRBS_DATA_H__
