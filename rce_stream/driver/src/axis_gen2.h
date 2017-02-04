/**
 *-----------------------------------------------------------------------------
 * Title      : AXIS Gen2 Functions
 * ----------------------------------------------------------------------------
 * File       : axis_gen2.h
 * Created    : 2017-02-03
 * ----------------------------------------------------------------------------
 * Description:
 * Access functions for Gen2 AXIS DMA
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
#ifndef __AXIS_GEN2_H__
#define __AXIS_GEN2_H__

#include "dma_include.h"
#include "dma_common.h"
#include "dma_buffer.h"
#include <linux/interrupt.h>

struct AxisG2Reg {
   uint32_t enableVer;       // 0x0000
   uint32_t intEnable;       // 0x0004
   uint32_t contEnable;      // 0x0008
   uint32_t dropEnable;      // 0x000C
   uint32_t wrBaseAddrLow;   // 0x0010
   uint32_t wrBaseAddrHigh;  // 0x0014
   uint32_t rdBaseAddrLow;   // 0x0018
   uint32_t rdBaseAddrHigh;  // 0x001C
   uint32_t fifoReset;       // 0x0020
   uint32_t buffBaseAddr;    // 0x0024
   uint32_t maxSize;         // 0x0028
   uint32_t online;          // 0x002C
   uint32_t acknowledge;     // 0x0030
   uint32_t channelCount;    // 0x0034
   uint32_t readAddrWidth;   // 0x0038
   uint32_t writeAddrWidth;  // 0x003C
   uint32_t readFifoLow;     // 0x0040
   uint32_t readFifoHigh;    // 0x0044
   uint32_t writeFifo;       // 0x0048
   uint32_t spareB[4077];    // 0x004C - 0x3FFC
   uint32_t writeAddr[4096]; // 0x4000 - 0x7FFC
   uint32_t readAddr[4096];  // 0x8000 - 0xFFFC
};

struct AxisG2Data {
   uint64_t  * readAddr;
   dma_addr_t  readHandle;
   uint32_t    readIndex;
   uint32_t    readCount;

   uint64_t  * writeAddr;
   dma_addr_t  writeHandle;
   uint32_t    writeIndex;
   uint32_t    writeCount;
};

// Get version
uint8_t getVersion();

// Get channel count
uint32_t getChannelCount();

// Get read count
uint32_t getReadCount();

// Get write count
uint32_t getWriteCount();

// Interrupt handler
irqreturn_t AxisG2_Irq(int irq, void *dev_id);

// Init card in top level Probe
void AxisG2_Init(struct DmaDevice *dev);

// Clear card in top level Remove
void AxisG2_Clear(struct DmaDevice *dev);

// Return receive buffer to card
void AxisG2_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer *buff);

// Send a buffer
int32_t AxisG2_SendBuffer(struct DmaDevice *dev, struct DmaBuffer *buff);

// Execute command
int32_t AxisG2_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);

// Add data to proc dump
void AxisG2_SeqShow(struct seq_file *s, struct DmaDevice *dev);

// Set functions for gen2 card
extern struct hardware_functions AxisG2_functions;

#endif

