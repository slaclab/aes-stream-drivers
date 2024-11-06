/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    This class is designed for generating and receiving PRBS (Pseudo-Random
 *    Binary Sequence) test data, primarily used for testing data integrity
 *    and communication channels.
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

#include "PrbsData.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include <cstdio>

// Constructor with specific width and tap counts
PrbsData::PrbsData(uint32_t width, uint32_t tapCnt, ...) {
   va_list a_list;
   uint32_t x;

   // Initialize PRBS parameters
   _sequence = 0;
   _width = width;
   _tapCnt = tapCnt;

   // Allocate memory for tap positions
   _taps = (uint32_t *)malloc(sizeof(uint32_t) * _tapCnt);

   // Set tap positions
   va_start(a_list, tapCnt);
   for (x = 0; x < tapCnt; x++) {
      _taps[x] = va_arg(a_list, uint32_t);
   }
   va_end(a_list);
}

// Default constructor
PrbsData::PrbsData() {
   // Default PRBS parameters
   _sequence = 0;
   _width = 32;
   _tapCnt = 4;

   // Allocate and set default tap positions
   _taps = (uint32_t *)malloc(sizeof(uint32_t) * _tapCnt);
   _taps[0] = 1;
   _taps[1] = 2;
   _taps[2] = 6;
   _taps[3] = 31;
}

// Destructor
PrbsData::~PrbsData() {
   // Free allocated memory for taps
   free(_taps);
}

// Generate PRBS data
void PrbsData::genData(const void *data, uint32_t size) {
   uint32_t word;
   uint32_t value;
   uint32_t *data32;
   uint16_t *data16;

   // Cast input data to appropriate type
   data32 = (uint32_t *)data;
   data16 = (uint16_t *)data;

   value = _sequence;

   // Handle different data widths
   if (_width == 16) {
      // Check size constraints for 16-bit width
      if ((size % 2) != 0 || size < 6) return;
      data16[0] = _sequence & 0xFFFF;
      data16[1] = (size - 2) / 2;
   } else if (_width == 32) {
      // Check size constraints for 32-bit width
      if ((size % 4) != 0 || size < 12) return;
      data32[0] = _sequence;
      data32[1] = (size - 4) / 4;
   } else {
      fprintf(stderr, "Bad gen width = %i\n", _width);
      return;
   }

   // Fill data with PRBS sequence
   for (word = 2; word < size / (_width / 8); word++) {
      value = flfsr(value);
      if (_width == 16) data16[word] = value;
      else if (_width == 32) data32[word] = value;
   }

   // Update sequence
   if (_width == 16) _sequence = data16[0] + 1;
   else if (_width == 32) _sequence = data32[0] + 1;
}

// Process received PRBS data
bool PrbsData::processData(const void *data, uint32_t size) {
   uint32_t eventLength;
   uint32_t expected;
   uint32_t got;
   uint32_t word;
   uint32_t min;
   uint32_t *data32;
   uint16_t *data16;

   // Cast input data to appropriate type
   data32 = (uint32_t *)data;
   data16 = (uint16_t *)data;

   // Handle different data widths
   if (_width == 16) {
      expected = data16[0];
      eventLength = (data16[1] * 2) + 2;
      min = 6;
   } else if (_width == 32) {
      expected = data32[0];
      eventLength = (data32[1] * 4) + 4;
      min = 12;
   } else {
      fprintf(stderr, "Bad process width = %i\n", _width);
      return false;
   }

   // Verify size constraints
   if (size < min || eventLength != size) {
      fprintf(stderr, "Bad size. exp=%i, min=%i, got=%i\n", eventLength, min, size);
      return false;
   }

   // Check sequence continuity
   if (_sequence != 0 && expected != 0 && _sequence != expected) {
      fprintf(stderr, "Bad Sequence. exp=%i, got=%i\n", _sequence, expected);
      _sequence = expected + 1;
      return false;
   }
   _sequence = expected + 1;

   // Verify PRBS sequence
   for (word = 2; word < size / (_width / 8); word++) {
      expected = flfsr(expected);
      if (_width == 16) {
          got = data16[word];
      } else if (_width == 32) {
          got = data32[word];
      } else {
          got = 0;
      }

      if (expected != got) {
         fprintf(stderr, "Bad value at index %i. exp=0x%x, got=0x%x\n", word, expected, got);
         return false;
      }
   }
   return true;
}

// Linear feedback shift register function
uint32_t PrbsData::flfsr(uint32_t input) {
   uint32_t bit = 0;
   uint32_t x;

   // Compute feedback bit
   for (x = 0; x < _tapCnt; x++) bit ^= (input >> _taps[x]) & 1;

   // Shift input and insert feedback bit
   return (input << 1) | bit;
}
