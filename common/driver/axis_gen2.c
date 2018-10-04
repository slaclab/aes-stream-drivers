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


// Map return descriptors to return record
inline uint8_t AxisG2_MapReturn ( struct AxisG2Return *ret, uint32_t desc128En, uint32_t index, uint32_t *ring) {
   uint32_t * ptr = (ring + (index*(desc128En?4:2)));
   uint32_t chan;
   uint32_t dest;

   if ( desc128En ) {
      if ( ptr[3] == 0 ) return 0;

      chan        = (ptr[3] >> 8) & 0xF;
      dest        = (ptr[3] & 0xFF);
      ret->dest   = (chan * 256) + dest;
      ret->size   = ptr[2];
      ret->index  = ptr[1];
      ret->fuser  = (ptr[0] >> 16) & 0xFF;
      ret->luser  = (ptr[0] >>  8) & 0xFF;
      ret->cont   = (ptr[0] >>  3) & 0x1;
      ret->result = ptr[0] & 0x7;

   } else {
      if ( ptr[1] == 0 ) return 0;

      ret->dest   = (ptr[1] >> 24) & 0xFF;
      ret->size   = (ptr[1] & 0xFFFFFF);
      ret->fuser  = (ptr[0] >> 24) & 0xFF;
      ret->luser  = (ptr[0] >> 16) & 0xFF;
      ret->index  = (ptr[0] >>  4) & 0xFFF;
      ret->cont   = (ptr[0] >>  3) & 0x1;
      ret->result = ptr[0] & 0x7;
   }

   memset(ptr,0,(desc128En?16:8));
   return 1;
}

// Add buffer to free list
inline void AxisG2_WriteFree ( struct DmaBuffer *buff, struct AxisG2Reg *reg, uint32_t desc128En ) {
   uint32_t wrData[2];

   wrData[0] = buff->index & 0x0FFFFFFF;

   if ( desc128En ) {
      wrData[0] |= (buff->buffHandle << 24) & 0x0FFFFFFF; // Addr bits 7:4 
      wrData[1]  = (buff->buffHandle >>  8) & 0xFFFFFFFF; // Addr bits 39:8

      iowrite32(wrData[1],&(reg->writeFifoB));
   }
   else iowrite32(buff->buffHandle,&(reg->dmaAddr[buff->index])); // Address table

   iowrite32(wrData[0],&(reg->writeFifoA));
}

// Add buffer to tx list
inline void AxisG2_WriteTx ( struct DmaBuffer *buff, struct AxisG2Reg *reg, uint32_t desc128En ) {
   uint32_t rdData[4];
   uint32_t dest;
   uint32_t chan;

   rdData[0]  = (buff->flags >> 13) & 0x00000008; // bit[3] = continue = flags[16]
   rdData[0] |= (buff->flags <<  8) & 0x00FF0000; // Bits[23:16] = lastUser = flags[15:8]
   rdData[0] |= (buff->flags << 24) & 0xFF000000; // Bits[31:24] = firstUser = flags[7:0]

   if ( desc128En ) {
      dest = buff->dest % 256;
      chan = buff->dest / 256;

      rdData[0] |= (dest << 4) & 0x000000F0;
      rdData[0] |= (chan << 8) & 0x0000FF00;

      rdData[1] = buff->size;

      rdData[2] = buff->index & 0x0FFFFFFF;
      rdData[2] |= (buff->buffHandle << 24) & 0xF0000000; // Addr bits 7:4 
      rdData[3]  = (buff->buffHandle >>  8) & 0xFFFFFFFF; // Addr bits 39:8

      iowrite32(rdData[3],&(reg->readFifoD));
      iowrite32(rdData[2],&(reg->readFifoC));
   }
   else {

      rdData[0] |= (buff->index <<  4) & 0x0000FFF0; // Bits[15:4] = buffId 

      rdData[1]  = buff->size & 0x00FFFFFF;   // bits[23:0]  = size
      rdData[1] |= (buff->dest << 24) & 0xFF000000; // bits[31:24] = dest

      iowrite32(buff->buffHandle,&(reg->dmaAddr[buff->index])); // Address table
   }

   iowrite32(rdData[1],&(reg->readFifoB));
   iowrite32(rdData[0],&(reg->readFifoA));
}


// Interrupt handler
irqreturn_t AxisG2_Irq(int irq, void *dev_id) {
   uint32_t handleCount;

   struct DmaDesc     * desc;
   struct DmaBuffer   * buff;
   struct DmaDevice   * dev;
   struct AxisG2Reg   * reg;
   struct AxisG2Data  * hwData;

   struct AxisG2Return ret;

   dev    = (struct DmaDevice *)dev_id;
   reg    = (struct AxisG2Reg *)dev->reg;
   hwData = (struct AxisG2Data *)dev->hwData;

   // Disable interrupt
   iowrite32(0x0,&(reg->intEnable));
   handleCount = 0;

   if ( dev->debug > 0 ) dev_info(dev->device,"Irq: Called.\n");

   // Grab locks now to avoid multiple lock/unlock cycles
   spin_lock(&dev->writeHwLock);
   spin_lock(&dev->maskLock);

   // Check read returns
   while ( AxisG2_MapReturn(&ret,hwData->desc128En,hwData->readIndex,hwData->readAddr) ) {
      handleCount++;
      if ( dev->debug > 0 ) dev_info(dev->device,"Irq: Got TX Descriptor: Idx=%i, Pos=%i\n",ret.index,hwData->readIndex);
      --(hwData->hwRdBuffCnt);

      // Attempt to find buffer in tx pool and return. otherwise return rx entry to hw.
      // Must adjust counters here and check for buffer need
      if ((buff = dmaRetBufferIdxIrq (dev,ret.index)) != NULL) {

         // Add to software queue
         if ( hwData->hwWrBuffCnt >= hwData->addrCount ) dmaQueuePushNoLock(&(hwData->wrQueue),buff);

         // Add to hardware queue
         else {
            ++(hwData->hwWrBuffCnt);
            AxisG2_WriteFree(buff,reg,hwData->desc128En);
         }
      }

      hwData->readIndex = ((hwData->readIndex+1) % hwData->addrCount);
      if ( handleCount > 4000 ) break;
   }

   // Check write descriptor
   while ( AxisG2_MapReturn(&ret,hwData->desc128En,hwData->writeIndex,hwData->writeAddr) ) {
      handleCount++;

      if ( dev->debug > 0 ) dev_info(dev->device,"Irq: Got RX Descriptor: Idx=%i, Pos=%i\n",ret.index,hwData->writeIndex);

      if ( (buff = dmaGetBufferList(&(dev->rxBuffers),ret.index)) != NULL ) {
         buff->count++;

         buff->size  = ret.size;
         buff->dest  = ret.dest;
         buff->error = (ret.size == 0)?DMA_ERR_FIFO:ret.result;

         buff->flags =  ret.fuser;                  // firstUser = flags[7:0]
         buff->flags |= (ret.luser << 8) & 0x0FF00; // lastUser = flags[15:8]
         buff->flags |= (ret.cont << 16) & 0x10000; // continue = flags[16]

         hwData->contCount += ret.cont;

         if ( dev->debug > 0 ) {
            dev_info(dev->device,"Irq: Rx size=%i, Dest=%i, fuser=%x, luser=%x, cont=%i, Error=0x%x\n",
               ret.size, ret.dest, ret.fuser, ret.luser, ret.cont, buff->error);
         }

         // Find owner of lane/vc
         if ( buff->dest < DMA_MAX_DEST ) desc = dev->desc[buff->dest];
         else desc = NULL;

         // Return entry to FPGA if destc is not open
         if ( desc == NULL ) {
            if ( dev->debug > 0 ) {
               dev_info(dev->device,"Irq: Port not open return to free list.\n");
            }
            AxisG2_WriteFree(buff,reg,hwData->desc128En);
         }

         // lane/vc is open,  Add to RX Queue
         else {
            --(hwData->hwWrBuffCnt);
            dmaRxBuffer(desc,buff);
         }
      }
      else dev_warn(dev->device,"Irq: Failed to locate RX buffer index %i.\n", ret.index);

      hwData->writeIndex = ((hwData->writeIndex+1) % hwData->addrCount);
      if ( handleCount > 4000 ) break;
   }

   // Write buffer queue
   while ( (hwData->hwWrBuffCnt < hwData->addrCount) && ((buff = dmaQueuePopNoLock(&(hwData->wrQueue))) != NULL ) ) {
      ++(hwData->hwWrBuffCnt);
      AxisG2_WriteFree(buff,reg,hwData->desc128En);
   }

   // Read buffer queue
   while ( (hwData->hwRdBuffCnt < hwData->addrCount) && ((buff = dmaQueuePopNoLock(&(hwData->rdQueue))) != NULL ) ) {
      ++(hwData->hwRdBuffCnt);
      AxisG2_WriteTx(buff,reg,hwData->desc128En);
   }

   // Unlock
   spin_unlock(&dev->maskLock);
   spin_unlock(&dev->writeHwLock);

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

   // 64-bit or 128-bit mode
   hwData->desc128En = ((ioread32(&(reg->enableVer)) & 0x10000) != 0);

   // Keep track of the number of buffers in hardware
   hwData->hwWrBuffCnt = 0;
   hwData->hwRdBuffCnt = 0;

   // Init software buffer queues
   dmaQueueInit(&hwData->wrQueue,dev->rxBuffers.count);
   dmaQueueInit(&hwData->rdQueue,dev->txBuffers.count + dev->rxBuffers.count);

   // Set read and write ring buffers
   hwData->addrCount = (1 << ioread32(&(reg->addrWidth)));

   if(dev->cfgMode & AXIS2_RING_ACP) {
      hwData->readAddr = kmalloc(hwData->addrCount*(hwData->desc128En?16:8), GFP_DMA | GFP_KERNEL);
      hwData->readHandle = virt_to_phys(hwData->readAddr);

      hwData->writeAddr = kmalloc(hwData->addrCount*(hwData->desc128En?16:8), GFP_DMA | GFP_KERNEL);
      hwData->writeHandle = virt_to_phys(hwData->writeAddr);
   }
   else {
      hwData->readAddr = 
         dma_alloc_coherent(dev->device, hwData->addrCount*(hwData->desc128En?16:8), &(hwData->readHandle),GFP_KERNEL);

      hwData->writeAddr = 
         dma_alloc_coherent(dev->device, hwData->addrCount*(hwData->desc128En?16:8), &(hwData->writeHandle),GFP_KERNEL);
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

      // Map failure
      if ( dmaBufferToHw(buff) < 0 ) dev_warn(dev->device,"Init: Failed to map dma buffer.\n");

      // Add to software queue
      else if ( hwData->hwWrBuffCnt >= hwData->addrCount ) dmaQueuePushNoLock(&(hwData->wrQueue),buff);

      // Add to hardware queue
      else {
         ++hwData->hwWrBuffCnt;
         AxisG2_WriteFree(buff,reg,hwData->desc128En);
      }
   }

   dev_info(dev->device,"Init: Found Version 2 Device. Desc128En=%i\n",hwData->desc128En);
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
   struct AxisG2Data *hwData;
   unsigned long iflags;

   reg = (struct AxisG2Reg *)dev->reg;
   hwData = (struct AxisG2Data *)dev->hwData;

   // Map failure
   if ( dmaBufferToHw(buff) < 0 ) dev_warn(dev->device,"RetRxBuffer: Failed to map dma buffer.\n");

   else {
      spin_lock_irqsave(&dev->writeHwLock,iflags);

      // Add to software queue
      if ( hwData->hwWrBuffCnt >= hwData->addrCount ) dmaQueuePushNoLock(&(hwData->wrQueue),buff);

      // Add to hardware queue
      else {
         ++(hwData->hwWrBuffCnt);
         AxisG2_WriteFree(buff,reg,hwData->desc128En);
      }
      spin_unlock_irqrestore(&dev->writeHwLock,iflags);
   }
}


// Send a buffer
int32_t AxisG2_SendBuffer(struct DmaDevice *dev, struct DmaBuffer *buff) {
   struct AxisG2Data * hwData;
   struct AxisG2Reg *reg;
   unsigned long iflags;

   reg = (struct AxisG2Reg *)dev->reg;
   hwData = (struct AxisG2Data *)dev->hwData;

   // Mapping failure
   if ( dmaBufferToHw(buff) < 0 ) {
      dev_warn(dev->device,"SendBuffer: Failed to map dma buffer.\n");
      return(-1);
   }

   else {
      spin_lock_irqsave(&dev->writeHwLock,iflags);

      // Add to software queue
      if ( hwData->hwRdBuffCnt >= hwData->addrCount ) dmaQueuePushNoLock(&(hwData->rdQueue),buff);

      // Add to hardware
      else {
         ++(hwData->hwRdBuffCnt);
         AxisG2_WriteTx (buff, reg, hwData->desc128En);
      }
      spin_unlock_irqrestore(&dev->writeHwLock,iflags);
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
   seq_printf(s,"          Address Count : %i\n",hwData->addrCount);
   seq_printf(s,"    Hw Write Buff Count : %i\n",hwData->hwWrBuffCnt);
   seq_printf(s,"     Hw Read Buff Count : %i\n",hwData->hwRdBuffCnt);
   seq_printf(s,"           Cache Config : 0x%x\n",(ioread32(&(reg->cacheConfig))));
   seq_printf(s,"            Desc 128 En : %i\n",hwData->desc128En);
}

