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

#include <dma_common.h>
#include <dma_buffer.h>
#include <linux/interrupt.h>

#define AXIS2_RING_ACP 0x10

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
   uint32_t spareA;          // 0x0024
   uint32_t maxSize;         // 0x0028
   uint32_t online;          // 0x002C
   uint32_t acknowledge;     // 0x0030
   uint32_t channelCount;    // 0x0034
   uint32_t addrWidth;       // 0x0038
   uint32_t cacheConfig;     // 0x003C
   uint32_t readFifoA;       // 0x0040
   uint32_t readFifoB;       // 0x0044
   uint32_t writeFifoA;      // 0x0048
   uint32_t intAckAndEnable; // 0x004C
   uint32_t intReqCount;     // 0x0050
   uint32_t hwWrIndex;       // 0x0054
   uint32_t hwRdIndex;       // 0x0058
   uint32_t wrReqMissed;     // 0x005C
   uint32_t readFifoC;       // 0x0060
   uint32_t readFifoD;       // 0x0064
   uint32_t spareB[2];       // 0x0068 - 0x006C
   uint32_t writeFifoB;      // 0x0070
   uint32_t spareC[3];       // 0x0074 - 0x007C
   uint32_t forceInt;        // 0x0080
   uint32_t irqHoldOff;      // 0x0084

   uint32_t spareD[2];       // 0x0088 - 0x008C
   uint32_t bgThold[8];      // 0x0090 - 0x00AC
   uint32_t bgCount[8];      // 0x00B0 - 0x00CC

   uint32_t spareE[4044];    // 0x00D0 - 0x3FFC

   uint32_t dmaAddr[4096];   // 0x4000 - 0x7FFC
};

struct AxisG2Return {
   uint32_t index;
   uint32_t size;
   uint8_t  result;
   uint8_t  fuser;
   uint8_t  luser;
   uint16_t dest;
   uint8_t  cont;
   uint8_t  id;
};

struct AxisG2Data {
   uint32_t    desc128En;

   uint32_t  * readAddr;
   dma_addr_t  readHandle;
   uint32_t    readIndex;

   uint32_t  * writeAddr;
   dma_addr_t  writeHandle;
   uint32_t    writeIndex;

   uint32_t    addrCount;
   uint32_t    missedIrq;

   uint32_t    hwWrBuffCnt;
   uint32_t    hwRdBuffCnt;

   struct DmaQueue wrQueue;
   struct DmaQueue rdQueue;

   uint32_t    contCount;

   uint32_t    irqHold;

   uint32_t    bgEnable;
   uint32_t    bgThold[8];
};

// Map return
inline uint8_t AxisG2_MapReturn ( struct DmaDevice *dev, struct AxisG2Return *ret, uint32_t desc128En, uint32_t index, uint32_t *ring);

// Add buffer to free list
inline void AxisG2_WriteFree ( struct DmaBuffer *buff, struct AxisG2Reg *reg, uint32_t desc128En );

// Add buffer to tx list
inline void AxisG2_WriteTx ( struct DmaBuffer *buff, struct AxisG2Reg *reg, uint32_t desc128En );

// Interrupt handler
irqreturn_t AxisG2_Irq(int irq, void *dev_id);

// Init card in top level Probe
void AxisG2_Init(struct DmaDevice *dev);

// enable device
void AxisG2_Enable(struct DmaDevice *dev);

// Clear card in top level Remove
void AxisG2_Clear(struct DmaDevice *dev);

// Return receive buffers to card
void AxisG2_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer **buff, uint32_t count);

// Send a buffer
int32_t AxisG2_SendBuffer(struct DmaDevice *dev, struct DmaBuffer **buff, uint32_t count);

// Execute command
int32_t AxisG2_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);

// Add data to proc dump
void AxisG2_SeqShow(struct seq_file *s, struct DmaDevice *dev);

// Set functions for gen2 card
extern struct hardware_functions AxisG2_functions;

#endif

