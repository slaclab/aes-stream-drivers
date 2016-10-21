/**
 *-----------------------------------------------------------------------------
 * Title      : AXIS Gen1 Functions
 * ----------------------------------------------------------------------------
 * File       : axis_gen1.h
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * Access functions for Gen1 AXIS DMA
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
#ifndef __AXIS_GEN1_H__
#define __AXIS_GEN1_H__

#include "dma_include.h"
#include "dma_common.h"
#include "dma_buffer.h"
#include <linux/interrupt.h>

struct AxisG1Reg {
   uint32_t rxEnable;        // 0x00000
   uint32_t txEnable;        // 0x00004
   uint32_t fifoClear;       // 0x00008
   uint32_t intEnable;       // 0x0000C
   uint32_t fifoValid;       // 0x00010
   uint32_t maxRxSize;       // 0x00014
   uint32_t onlineAck;       // 0x00018
   uint32_t intPendAck;      // 0x0001C
   uint32_t spareA[16384-8]; // 0x00020 - 0x0FFFC
   uint32_t rxPend;          // 0x10000
   uint32_t txFree;          // 0x10004
   uint32_t spareB[126];     // 0x10008 - 0x101FC
   uint32_t rxFree;          // 0x10200
   uint32_t spaceC[15];      // 0x10204 - 0x1023C
   uint32_t txPostA;         // 0x10240
   uint32_t txPostB;         // 0x10244
   uint32_t txPostC;         // 0x10248
   uint32_t txPass;          // 0x1024C
};

// Interrupt handler
irqreturn_t AxisG1_Irq(int irq, void *dev_id);

// Init card in top level Probe
void AxisG1_Init(struct DmaDevice *dev);

// Clear card in top level Remove
void AxisG1_Clear(struct DmaDevice *dev);

// Return receive buffer to card
void AxisG1_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer *buff);

// Send a buffer
int32_t AxisG1_SendBuffer(struct DmaDevice *dev, struct DmaBuffer *buff);

// Execute command
int32_t AxisG1_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);

// Add data to proc dump
void AxisG1_SeqShow(struct seq_file *s, struct DmaDevice *dev);

// Set functions for gen2 card
extern struct hardware_functions AxisG1_functions;

#endif

