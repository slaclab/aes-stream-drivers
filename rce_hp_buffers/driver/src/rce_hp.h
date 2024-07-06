/**
 *-----------------------------------------------------------------------------
 * Title      : RCE HP Driver
 * ----------------------------------------------------------------------------
 * File       : rce_hp.h
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the aes_stream_drivers package, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 * ----------------------------------------------------------------------------
**/
#ifndef __RCE_HP_H__
#define __RCE_HP_H__

#include <linux/interrupt.h>

#include "dma_common.h"
#include "dma_buffer.h"

struct RceHpReg {
   uint32_t spare[100];      // skip  0x0 - 0x3FF
   uint32_t enable;          // 0x00400
   uint32_t bufferClear;     // 0x00404
   uint32_t bufferSize;      // 0x00408
   uint32_t bufferAlloc;     // 0x0040C
};

// Init card in top level Probe
void RceHp_Init(struct DmaDevice *dev);

// Enable
void RceHp_Enable(struct DmaDevice *dev);

// Clear card in top level Remove
void RceHp_Clear(struct DmaDevice *dev);

// Return receive buffer to card
void RceHp_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer **buff, uint32_t count);

// Send a buffer
int32_t RceHp_SendBuffer(struct DmaDevice *dev, struct DmaBuffer **buff, uint32_t count);

// Execute command
int32_t RceHp_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);

// Add data to proc dump
void RceHp_SeqShow(struct seq_file *s, struct DmaDevice *dev);

// Set functions for gen2 card
extern struct hardware_functions RceHp_functions;

#endif

