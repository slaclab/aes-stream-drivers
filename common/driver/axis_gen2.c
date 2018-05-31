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
#include <axis_gen2.h>
#include <AxisDriver.h>
#include <linux/seq_file.h>
#include <linux/signal.h>
#include <linux/slab.h>

#define BUFF_MODE_MIXED 

// Set functions for gen2 card
struct hardware_functions AxisG2_functions = {
   .irq          = AxisG2_Irq,
   .init         = AxisG2_Init,
   .enable       = AxisG2_Enable,
   .clear        = AxisG2_Clear,
   .retRxBuffer  = AxisG2_RetRxBuffer,
   .sendBuffer   = AxisG2_SendBuffer,
   .command      = AxisG2_Command,
   .seqShow      = AxisG2_SeqShow,
};

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

   if ( dev->debug > 0 ) 
      dev_info(dev->device,"Irq: Called.\n");

   // Check read returns
   while ( (dmaData = hwData->readAddr[hwData->readIndex]) != 0 ) {
      index = (dmaData >> 4) & 0xFFF;
      handleCount++;
      if ( dev->debug > 0 ) 
         dev_info(dev->device,"Irq: Got TX Descriptor: 0x%.16llx, Idx=%i, Pos=%i\n",dmaData,index,hwData->readIndex);

      // Attempt to find buffer in tx pool and return. otherwise return rx entry to hw.
      if ((buff = dmaRetBufferIdxIrq (dev,index)) != NULL) {
         iowrite32(buff->index,&(reg->writeFifo));
      }

      memset(&(hwData->readAddr[hwData->readIndex]),0,8);
      hwData->readIndex = ((hwData->readIndex+1) % hwData->addrCount);
   }

   // Check write descriptor
   while ( (dmaData = hwData->writeAddr[hwData->writeIndex]) != 0 ) {
      index = (dmaData >> 4) & 0xFFF;
      handleCount++;

      if ( dev->debug > 0 ) 
         dev_info(dev->device,"Irq: Got RX Descriptor: 0x%.16llx, Idx=%i, Pos=%i\n",dmaData,index,hwData->writeIndex);

      if ( (buff = dmaGetBufferList(&(dev->rxBuffers),index)) != NULL ) {
         buff->count++;
         buff->size  = (dmaData >> 32) & 0xFFFFFF;
         buff->dest  = (dmaData >> 56) & 0xFF;
         buff->error = (buff->size == 0)?DMA_ERR_FIFO:(dmaData & 0x7);

         buff->flags =  (dmaData >> 24) & 0xFF; // Bits[31:24] = firstUser = flags[7:0]
         buff->flags += (dmaData >> 8) & 0xFF00; // Bits[23:16] = lastUser = flags[15:8]
         buff->flags += (dmaData << 13) & 0x10000; // bit[3] = continue = flags[16]

         if (dmaData & 0x8) hwData->contCount++;

         if ( dev->debug > 0 ) {
            dev_info(dev->device,"Irq: Rx size=%i, Dest=%i, Flags=0x%x, Error=0x%x\n",
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
         else dmaRxBuffer(desc,buff);

         // Unlock
         spin_unlock(&dev->maskLock);
      }
      else 
         dev_warn(dev->device,"Irq: Failed to locate RX buffer index %i.\n", index);

      memset(&(hwData->writeAddr[hwData->writeIndex]),0,8);
      hwData->writeIndex = ((hwData->writeIndex+1) % hwData->addrCount);
   }

   // Enable interrupt and update ack count
   iowrite32(0x30000 + handleCount,&(reg->intAckAndEnable));
   if ( dev->debug > 0 ) dev_info(dev->device,"Irq: Done. Handled = %i\n",handleCount);
   if ( handleCount == 0 ) hwData->missedIrq++;
   return(IRQ_HANDLED);
}

// Init card in top level Probe
void AxisG2_Init(struct DmaDevice *dev) {
   uint32_t x;

   struct DmaBuffer  *buff;
   struct AxisG2Data *hwData;
   struct AxisG2Reg  *reg;

   reg = (struct AxisG2Reg *)dev->reg;

   // Dest mask
   memset(dev->destMask,0xFF,DMA_MASK_SIZE);

   // Init hw data
   hwData = (struct AxisG2Data *)kmalloc(sizeof(struct AxisG2Data),GFP_KERNEL);
   dev->hwData = hwData;

   // Set read and write ring buffers
   hwData->addrCount = (1 << ioread32(&(reg->addrWidth)));

   if(dev->cfgMode & AXIS2_RING_ACP) {
      hwData->readAddr = kmalloc(hwData->addrCount*8, GFP_DMA | GFP_KERNEL);
      hwData->readHandle = virt_to_phys(hwData->readAddr);

      hwData->writeAddr = kmalloc(hwData->addrCount*8, GFP_DMA | GFP_KERNEL);
      hwData->writeHandle = virt_to_phys(hwData->writeAddr);
   }
   else {
      hwData->readAddr = 
         dma_alloc_coherent(dev->device, hwData->addrCount*8, &(hwData->readHandle),GFP_KERNEL);

      hwData->writeAddr = 
         dma_alloc_coherent(dev->device, hwData->addrCount*8, &(hwData->writeHandle),GFP_KERNEL);
   }

   dev_info(dev->device,"Init: Read  ring at: %p\n",(void *)hwData->readHandle);
   dev_info(dev->device,"Init: Write ring at: %p\n",(void *)hwData->writeHandle);

   // Init and set ring address
   iowrite32(hwData->readHandle,&(reg->rdBaseAddrLow));
   memset(hwData->readAddr,0,hwData->addrCount*8);
   hwData->readIndex = 0;

   // Init and set ring address
   iowrite32(hwData->writeHandle,&(reg->wrBaseAddrLow));
   memset(hwData->writeAddr,0,hwData->addrCount*8);
   hwData->writeIndex = 0;

   hwData->missedIrq = 0;
   hwData->contCount = 0;

   // Set cache mode, bits3:0 = descWr, bits 11:8 = bufferWr, bits 15:12 = bufferRd
   x = 0;
   if ( dev->cfgMode & BUFF_ARM_ACP   ) x |= 0xA600; // Buffer
   if ( dev->cfgMode & AXIS2_RING_ACP ) x |= 0x00A6; // Desc
   iowrite32(x,&(reg->cacheConfig));
   
   // Set MAX RX                      
   iowrite32(dev->cfgSize,&(reg->maxSize));

   // Enable
   iowrite32(0x1,&(reg->enableVer));

   // Clear FIFOs                     
   iowrite32(0x1,&(reg->fifoReset)); 
   iowrite32(0x0,&(reg->fifoReset)); 

   // Continue and drop
   iowrite32(0x1,&(reg->contEnable)); 
   iowrite32(0x0,&(reg->dropEnable)); 

   // Push RX buffers to hardware and map
   for (x=0; x < dev->rxBuffers.count; x++) {
      buff = dev->rxBuffers.indexed[x];
      if ( dmaBufferToHw(buff) < 0 ) 
         dev_warn(dev->device,"Init: Failed to map dma buffer.\n");
      else {
         iowrite32(buff->index,&(reg->writeFifo)); // Free List
         iowrite32(buff->buffHandle,&(reg->dmaAddr[buff->index])); // Address table
      }
   }

   // Map TX buffers
   for (x=0; x < dev->txBuffers.count; x++) {
      buff = dev->txBuffers.indexed[x];
      iowrite32(buff->buffHandle,&(reg->dmaAddr[buff->index])); // Address table
   }

   dev_info(dev->device,"Init: Found Version 2 Device.\n");
}


// Enable the card
void AxisG2_Enable(struct DmaDevice *dev) {
   struct AxisG2Reg  *reg;

   reg = (struct AxisG2Reg *)dev->reg;

   // Online
   iowrite32(0x1,&(reg->online));

   // Enable interrupt
   iowrite32(0x1,&(reg->intEnable));
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

   // Free buffers
   if(dev->cfgMode & AXIS2_RING_ACP) {
      kfree(hwData->readAddr);
      kfree(hwData->writeAddr);
   }
   else {
      dma_free_coherent(dev->device, hwData->addrCount*8, hwData->writeAddr, hwData->writeHandle);
      dma_free_coherent(dev->device, hwData->addrCount*8, hwData->readAddr, hwData->readHandle);
   }

   kfree(hwData);
}


// Return receive buffer to card
// Single write so we don't need to lock
void AxisG2_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer *buff) {
   struct AxisG2Reg *reg;
   reg = (struct AxisG2Reg *)dev->reg;

   if ( dmaBufferToHw(buff) < 0 ) 
      dev_warn(dev->device,"RetRxBuffer: Failed to map dma buffer.\n");
   else {
      iowrite32(buff->buffHandle,&(reg->dmaAddr[buff->index])); // Address table
      iowrite32(buff->index,&(reg->writeFifo));
   }
}


// Send a buffer
int32_t AxisG2_SendBuffer(struct DmaDevice *dev, struct DmaBuffer *buff) {
   uint32_t descLow;
   uint32_t descHigh;

   struct AxisG2Data * hwData;
   struct AxisG2Reg *reg;

   reg = (struct AxisG2Reg *)dev->reg;
   hwData = (struct AxisG2Data *)dev->hwData;

   // Create descriptor
   descLow  = (buff->flags >> 13) & 0x00000008; // bit[0] = continue = flags[16]
   descLow += (buff->index <<  4) & 0x0000FFF0; // Bits[15:4] = buffId 
   descLow += (buff->flags <<  8) & 0x00FF0000; // Bits[23:16] = lastUser = flags[15:8]
   descLow += (buff->flags << 24) & 0xFF000000; // Bits[31:24] = firstUser = flags[7:0]
    
   descHigh  = buff->size & 0x00FFFFFF;   // bits[23:0]  = size
   descHigh += (buff->dest << 24) & 0xFF000000; // bits[31:24] = dest

   if ( dmaBufferToHw(buff) < 0 ) {
      dev_warn(dev->device,"SendBuffer: Failed to map dma buffer.\n");
      return(-1);
   }
   iowrite32(buff->buffHandle,&(reg->dmaAddr[buff->index])); // Address table

   // Write to hardware, order of writes do not mapper
   spin_lock(&dev->writeHwLock);
   iowrite32(descLow,&(reg->readFifoLow));
   iowrite32(descHigh,&(reg->readFifoHigh));
   spin_unlock(&dev->writeHwLock);

   if ( dev->debug > 0 ) {
      dev_info(dev->device,"SendBuffer: Wrote High=0x%x, Low=0x%x, Handle=0x%llx\n",descHigh,descLow,buff->buffHandle);
      dev_info(dev->device,"SendBuffer: %x %x %x %x %x %x %x %x\n", ((uint8_t*)buff->buffAddr)[0], ((uint8_t*)buff->buffAddr)[1], ((uint8_t*)buff->buffAddr)[2],
                                                                    ((uint8_t*)buff->buffAddr)[3], ((uint8_t*)buff->buffAddr)[4], ((uint8_t*)buff->buffAddr)[5],
                                                                    ((uint8_t*)buff->buffAddr)[6], ((uint8_t*)buff->buffAddr)[7]);
   }
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
   seq_printf(s,"          Int Req Count : %u\n",(ioread32(&(reg->intReqCount))));
   seq_printf(s,"        Hw Dma Wr Index : %u\n",(ioread32(&(reg->hwWrIndex))));
   seq_printf(s,"        Sw Dma Wr Index : %u\n",hwData->writeIndex);
   seq_printf(s,"        Hw Dma Rd Index : %u\n",(ioread32(&(reg->hwRdIndex))));
   seq_printf(s,"        Sw Dma Rd Index : %u\n",hwData->readIndex);
   seq_printf(s,"     Missed Wr Requests : %u\n",(ioread32(&(reg->wrReqMissed))));
   seq_printf(s,"       Missed IRQ Count : %u\n",hwData->missedIrq);
   seq_printf(s,"         Continue Count : %u\n",hwData->contCount);
   seq_printf(s,"           Cache Config : 0x%x\n",(ioread32(&(reg->cacheConfig))));
}

