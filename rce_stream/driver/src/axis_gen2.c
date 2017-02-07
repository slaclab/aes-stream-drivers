/**
 *-----------------------------------------------------------------------------
 * Title      : AXIS Gen2 Functions
 * ----------------------------------------------------------------------------
 * File       : axis_gen2.c
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
#include "axis_gen2.h"
#include <linux/seq_file.h>
#include <linux/signal.h>
#include <linux/slab.h>

// Set functions for gen2 card
struct hardware_functions AxisG2_functions = {
   .irq          = AxisG2_Irq,
   .init         = AxisG2_Init,
   .clear        = AxisG2_Clear,
   .retRxBuffer  = AxisG2_RetRxBuffer,
   .sendBuffer   = AxisG2_SendBuffer,
   .command      = AxisG2_Command,
   .seqShow      = AxisG2_SeqShow,
};

// Get version
uint8_t AxisG2_GetVersion(struct DmaDevice *dev) {
   struct AxisG2Reg  *reg;
   uint32_t temp;

   reg = (struct AxisG2Reg *)dev->reg;

   temp = ioread32(&(reg->enableVer));
   return((temp >> 24) & 0xFF);
}

// Interrupt handler
irqreturn_t AxisG2_Irq(int irq, void *dev_id) {
   uint32_t index;
   uint32_t handleCount;
   uint64_t dmaData;

   struct DmaDesc     * desc;
   struct DmaBuffer   * buff;
   struct DmaDevice   * dev;
   struct AxisG2Reg   * reg;
   struct AxisG2Data  * hwData;

   dev    = (struct DmaDevice *)dev_id;
   reg    = (struct AxisG2Reg *)dev->reg;
   hwData = (struct AxisG2Data *)dev->hwData;

   // Disable interrupt
   iowrite32(0x0,&(reg->intEnable));
   handleCount = 0;

   // Check read returns
   while ( (dmaData = hwData->readAddr[hwData->readIndex]) != 0 ) {
      index = (dmaData >> 4) & 0xFFF;
      handleCount++;

      if ( (buff = dmaGetBuffer(&(dev->rxBuffers),index)) != NULL ) {
         dmaBufferFromHw(buff);
         dmaQueuePushIrq(&(dev->tq),buff);

         if ( dev->debug > 0 ) 
            dev_info(dev->device,"Irq: Return TX buffer index %i.\n",index);
      }
      else 
         dev_warn(dev->device,"Irq: Failed to locate TX buffer index %i.\n", index);

      memset(&(hwData->readAddr[hwData->readIndex]),0,8);
      hwData->readIndex = (hwData->readIndex+1) % hwData->readCount;
   }

   // Check write descriptor
   while ( (dmaData = hwData->writeAddr[hwData->writeIndex]) != 0 ) {
      index = (dmaData >> 4) & 0xFFF;
      handleCount++;

      if ( (buff = dmaGetBuffer(&(dev->rxBuffers),index)) != NULL ) {
         buff->count++;
         buff->size  = (dmaData >> 32) & 0xFFFFFF;
         buff->dest  = (dmaData >> 56) & 0xFF;
         buff->error = (buff->size == 0)?DMA_ERR_FIFO:(dmaData & 0x3);

         buff->flags =  (dmaData >> 24) & 0xFF; // Bits[31:24] = firstUser = flags[7:0]
         buff->flags += (dmaData >> 8) & 0xFF; // Bits[23:16] = lastUser = flags[15:8]
         buff->flags += (dmaData << 13) & 0x10000; // bit[3] = continue = flags[16]
 
         if ( dev->debug > 0 ) {
            dev_info(dev->device,"Irq: Rx size=%i, Dest=%i, Flags=0x%x, Error=0x%x.\n",
               buff->size, buff->dest, buff->flags, buff->error);
         }

         // Lock mask records
         // This ensures close does not occur while irq routine is 
         // pushing data to desc rx queue
         spin_lock(&dev->maskLock);

         // Find owner of lane/vc
         if ( buff->dest < DMA_MAX_DEST ) desc = dev->desc[buff->dest];
         else desc = NULL;

         // Return entry to FPGA if destc is not open
         if ( desc == NULL ) {
            if ( dev->debug > 0 ) {
               dev_info(dev->device,"Irq: Port not open return to free list.\n");
            }
            iowrite32(buff->index,&(reg->writeFifo));
         }

         // lane/vc is open,  Add to RX Queue
         else {
            dmaBufferFromHw(buff);
            dmaQueuePushIrq(&(desc->q),buff);
            if (desc->async_queue) kill_fasync(&desc->async_queue, SIGIO, POLL_IN);
         }

         // Unlock
         spin_unlock(&dev->maskLock);
      }
      else 
         dev_warn(dev->device,"Irq: Failed to locate RX buffer index %i.\n", index);

      memset(&(hwData->writeAddr[hwData->writeIndex]),0,8);
      hwData->writeIndex = (hwData->writeIndex+1) % hwData->writeCount;
   }

   // Enable interrupt and update ack count
   iowrite32(0x10000 + handleCount,&(reg->intAckAndEnable));

   if ( handleCount ) return(IRQ_HANDLED);
   else return(IRQ_NONE);
}


// Init card in top level Probe
void AxisG2_Init(struct DmaDevice *dev) {
   uint32_t x;

   struct DmaBuffer  *buff;
   struct AxisG2Data *hwData;
   struct AxisG2Reg  *reg;

   reg = (struct AxisG2Reg *)dev->reg;

   // Init hw data
   hwData = (struct AxisG2Data *)kmalloc(sizeof(struct AxisG2Data),GFP_KERNEL);
   dev->hwData = hwData;

   // Create read address ring
   hwData->readAddr = 
      dma_alloc_coherent(dev->device, dev->txBuffers.size*8, &(hwData->readHandle),GFP_KERNEL);

   // Init and set ring address
   iowrite32(hwData->readHandle,&(reg->rdBaseAddrLow));
   memset(hwData->readAddr,0,dev->txBuffers.size*8);
   hwData->readIndex = 0;
   hwData->readCount = dev->txBuffers.size;

   // Create write address ring
   hwData->writeAddr = 
      dma_alloc_coherent(dev->device, dev->rxBuffers.size*8, &(hwData->writeHandle),GFP_KERNEL);

   // Init and set ring address
   iowrite32(hwData->writeHandle,&(reg->wrBaseAddrLow));
   memset(hwData->writeAddr,0,dev->txBuffers.size*8);
   hwData->writeIndex = 0;
   hwData->writeCount = dev->rxBuffers.size;

   // Set MAX RX                      
   iowrite32(dev->rxBuffers.size,&(reg->maxSize));
                                      
   // Clear FIFOs                     
   iowrite32(0x1,&(reg->fifoReset)); 
   iowrite32(0x0,&(reg->fifoReset)); 

   // Continue and drop
   iowrite32(0x1,&(reg->contEnable)); 
   iowrite32(0x0,&(reg->dropEnable)); 

   // Enable
   iowrite32(0x1,&(reg->enableVer));

   // Push RX buffers to hardware and map
   for (x=0; x < dev->rxBuffers.count; x++) {
      buff = dev->rxBuffers.indexed[x];
      if ( dmaBufferToHw(buff) == 0 ) {
         iowrite32(buff->index,&(reg->writeFifo)); // Free List
         iowrite32(buff->buffHandle,&(reg->writeAddr[buff->index])); // Address table
      }
      else dev_warn(dev->device,"Init: Failed to map dma buffer.\n");
   }

   // Map TX buffers
   for (x=0; x < dev->txBuffers.count; x++) {
      buff = dev->txBuffers.indexed[x];
      iowrite32(buff->buffHandle,&(reg->readAddr[buff->index])); // Address table
   }

   // Online
   iowrite32(0x1,&(reg->online));

   // Set dest mask
   dev->destMask = 0xFF;

   // Enable interrupt
   iowrite32(0x1,&(reg->intEnable));

   dev_info(dev->device,"Init: Found Version 2 Device.\n");
}


// Clear card in top level Remove
void AxisG2_Clear(struct DmaDevice *dev) {
   struct AxisG2Reg *reg;
   struct AxisG2Data * hwData;

   reg = (struct AxisG2Reg *)dev->reg;
   hwData = (struct AxisG2Data *)dev->hwData;

   // Disable interrupt
   iowrite32(0x0,&(reg->intEnable));

   // Disable rx and tx
   iowrite32(0x0,&(reg->enableVer));
   iowrite32(0x0,&(reg->online));

   // Clear FIFOs
   iowrite32(0x1,&(reg->fifoReset));

   // Free Buffers
   dma_free_coherent(dev->device, hwData->writeCount*8, hwData->writeAddr, hwData->writeHandle);
   dma_free_coherent(dev->device, hwData->readCount*8, hwData->readAddr, hwData->readHandle);

   kfree(hwData);
}


// Return receive buffer to card
// Single write so we don't need to lock
void AxisG2_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer *buff) {
   struct AxisG2Reg *reg;
   reg = (struct AxisG2Reg *)dev->reg;

   if ( dmaBufferToHw(buff) == 0 ) iowrite32(buff->index,&(reg->writeFifo));
   else dev_warn(dev->device,"RetRxBuffer: Failed to map dma buffer.\n");
}


// Send a buffer
int32_t AxisG2_SendBuffer(struct DmaDevice *dev, struct DmaBuffer *buff) {
   uint32_t descLow;
   uint32_t descHigh;

   struct AxisG2Reg *reg;
   reg = (struct AxisG2Reg *)dev->reg;

   // Create descriptor
   descLow  = (buff->flags >> 16) & 0x00000001; // bit[0] = continue = flags[16]
   descLow += (buff->index <<  4) & 0x0000FFF0; // Bits[15:4] = buffId 
   descLow += (buff->flags <<  8) & 0x00FF0000; // Bits[23:16] = lastUser = flags[15:8]
   descLow += (buff->flags << 24) & 0xFF000000; // Bits[31:24] = firstUser = flags[7:0]
    
   descHigh  = buff->size & 0x00FFFFFF;         // bits[23:0]  = size
   descHigh += (buff->dest << 24) & 0xFF000000; // bits[31:24] = dest
   
   if ( dmaBufferToHw(buff) ) {
      dev_warn(dev->device,"SendBuffer: Failed to map dma buffer.\n");
      return(-1);
   }

   // Write to hardware, order of writes do not mapper
   spin_lock(&dev->writeHwLock);
   iowrite32(descLow,&(reg->readFifoLow));
   iowrite32(descHigh,&(reg->readFifoHigh));
   spin_unlock(&dev->writeHwLock);

   return(buff->size);
}


// Execute command
int32_t AxisG2_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   struct AxisG2Reg *reg;
   reg = (struct AxisG2Reg *)dev->reg;

   switch (cmd) {

      // Read ACK
      case AXIS_Read_Ack:
         spin_lock(&dev->commandLock);
         iowrite32(0x1,&(reg->acknowledge));
         spin_unlock(&dev->commandLock);
         return(0);
         break;

      default:
         dev_warn(dev->device,"Command: Invalid command=%i\n",cmd); 
         return(-1);
         break;
   }
}


// Add data to proc dump
void AxisG2_SeqShow(struct seq_file *s, struct DmaDevice *dev) {
   struct AxisG2Reg *reg;
   struct AxisG2Data * hwData;

   reg = (struct AxisG2Reg *)dev->reg;
   hwData = (struct AxisG2Data *)dev->hwData;

   seq_printf(s,"\n");
   seq_printf(s,"-------------- General HW -----------------\n");
   seq_printf(s,"          Int Req Count : %i\n",(ioread32(&(reg->intReqCount))));
   seq_printf(s,"            Hw Wr Index : %i\n",(ioread32(&(reg->hwWrIndex))));
   seq_printf(s,"            Sw Wr Index : %i\n",hwData->writeIndex);
   seq_printf(s,"            Hw Rd Index : %i\n",(ioread32(&(reg->hwRdIndex))));
   seq_printf(s,"            Sw Rd Index : %i\n",hwData->readIndex);
}

