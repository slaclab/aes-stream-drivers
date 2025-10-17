/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the aes_stream_drivers package, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 * ----------------------------------------------------------------------------
**/
#include <AxisDriver.h>
#include <rce_hp.h>
#include <linux/seq_file.h>
#include <linux/signal.h>

// Set functions for gen2 card
struct hardware_functions RceHp_functions = {
   .irq          = NULL,
   .init         = RceHp_Init,
   .enable       = RceHp_Enable,
   .irqEnable    = RceHp_IrqEnable,
   .clear        = RceHp_Clear,
   .retRxBuffer  = RceHp_RetRxBuffer,
   .sendBuffer   = RceHp_SendBuffer,
   .command      = RceHp_Command,
   .seqShow      = RceHp_SeqShow,
};

// Init card in top level Probe
void RceHp_Init(struct DmaDevice *dev) {
   uint32_t x;

   struct DmaBuffer * buff;

   struct RceHpReg *reg;
   reg = (struct RceHpReg *)dev->reg;

   // Clear Buffers
   iowrite32(0x1, &(reg->bufferClear));
   iowrite32(0x0, &(reg->bufferClear));

   // Set buffer size
   iowrite32(dev->cfgSize, &(reg->bufferSize));

   // Push buffers to hardware
   for (x=dev->rxBuffers.baseIdx; x < (dev->rxBuffers.baseIdx + dev->rxBuffers.count); x++) {
      buff = dmaGetBufferList(&(dev->rxBuffers), x);
      if ( dmaBufferToHw(buff) < 0 ) {
         dev_warn(dev->device, "Init: Failed to map dma buffer.\n");
      } else {
          iowrite32(buff->buffHandle, &(reg->bufferAlloc));
      }
   }

   // Set dest mask
   memset(dev->destMask, 0x0, DMA_MASK_SIZE);
   dev_info(dev->device, "Init: Done.\n");
}

// Enable the card
void RceHp_Enable(struct DmaDevice *dev) {
   struct RceHpReg  *reg;
   reg = (struct RceHpReg *)dev->reg;

   // Enable
   iowrite32(0x1, &(reg->enable));
}

// Clear card in top level Remove
void RceHp_Clear(struct DmaDevice *dev) {
   struct RceHpReg *reg;
   reg = (struct RceHpReg *)dev->reg;

   // Clear FIFOs
   iowrite32(0x1, &(reg->bufferClear));

   // Disable
   iowrite32(0x0, &(reg->enable));
}

// Enable/disable IRQs in hardware
void RceHp_IrqEnable(struct DmaDevice *dev, int en) { }

// Return receive buffer to card
// Single write so we don't need to lock
void RceHp_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer **buff, uint32_t count) {}

// Send a buffer
int32_t RceHp_SendBuffer(struct DmaDevice *dev, struct DmaBuffer **buff, uint32_t count) {
   return 0;
}

// Execute command
int32_t RceHp_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   return 0;
}
// Add data to proc dump
void RceHp_SeqShow(struct seq_file *s, struct DmaDevice *dev) { }

