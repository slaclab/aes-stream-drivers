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
#include <AxisDriver.h>
#include <axis_gen1.h>
#include <linux/seq_file.h>
#include <linux/signal.h>

// Set functions for gen2 card
struct hardware_functions AxisG1_functions = {
   .irq          = AxisG1_Irq,
   .init         = AxisG1_Init,
   .clear        = AxisG1_Clear,
   .retRxBuffer  = AxisG1_RetRxBuffer,
   .sendBuffer   = AxisG1_SendBuffer,
   .command      = AxisG1_Command,
   .seqShow      = AxisG1_SeqShow,
};

// Interrupt handler
irqreturn_t AxisG1_Irq(int irq, void *dev_id) {
   uint32_t    stat;
   uint32_t    handle;
   uint32_t    size;
   uint32_t    status;

   struct DmaDesc     * desc;
   struct DmaBuffer   * buff;
   struct DmaDevice   * dev;
   struct AxisG1Reg   * reg;

   dev  = (struct DmaDevice *)dev_id;
   reg  = (struct AxisG1Reg *)dev->reg;

   // Read IRQ Status
   if ( ioread32(&(reg->intPendAck)) != 0 ) {

       // Ack interrupt
      iowrite32(0x1,&(reg->intPendAck));

      // Disable interrupts
      iowrite32(0x0,&(reg->intEnable));

      // Read from FIFOs
      while ( (stat = ioread32(&(reg->fifoValid))) != 0 ) {

         // Transmit return
         if ( (stat & 0x2) != 0 ) {

            // Read handle
            if (((handle = ioread32(&(reg->txFree))) & 0x80000000) != 0 ) {
               handle &= 0x7FFFFFFC;
              
               if ( dev->debug > 0 ) 
                  dev_info(dev->device,"Irq: Return TX Status Value 0x%.8x.\n",handle);

               // Attempt to find buffer in tx pool and return. otherwise return rx entry to hw.
               if ((buff = dmaRetBufferIrq (dev,handle)) != NULL) {
                  iowrite32(handle,&(reg->rxFree));
               }
            }
         }

         // Receive data
         if ( (stat & 0x1) != 0 ) {

            // Read handle
            while (((handle = ioread32(&(reg->rxPend))) & 0x80000000) != 0 ) {
               handle &= 0x7FFFFFFC;

               // Read size
               do { 
                  size = ioread32(&(reg->rxPend));
               } while ((size & 0x80000000) == 0);

               // Bad marker
               if ( (size & 0xFF000000) != 0xE0000000 ) {
                  dev_warn(dev->device,"Irq: Bad FIFO size marker 0x%.8x.\n", size);
                  size = 0;
               }
               else size &= 0xFFFFFF;

               // Get status
               do { 
                  status = ioread32(&(reg->rxPend));
               } while ((status & 0x80000000) == 0);

               // Bad marker
               if ( (status & 0xF0000000) != 0xF0000000 ) {
                  dev_warn(dev->device,"Irq: Bad FIFO status marker 0x%.8x.\n", status);
                  size = 0;
               }

               // Find RX buffer entry
               if ((buff = dmaFindBufferList (&(dev->rxBuffers),handle)) != NULL) {

                  // Extract data from descriptor
                  buff->count++;
                  buff->size  = size;
                  buff->flags = (status >>  8) & 0xFFFF; // 15:8 = luser, 7:0 = fuser
                  buff->dest  = (status      ) & 0xFF;
                  buff->error = (size == 0)?DMA_ERR_FIFO:0;

                  // Check for errors
                  if ( (status & 0x01000000) != 0 ) {
                     dev_info(dev->device,"Irq: AXI write error detected.\n");
                     buff->error |= DMA_ERR_BUS;
                  }
                  if ( (status & 0x02000000) != 0 ) {
                     dev_info(dev->device,"Irq: DMA overflow error detected.\n");
                     buff->error |= DMA_ERR_LEN;
                  }
               
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
                     iowrite32(handle,&(reg->rxFree));
                  }

                  // lane/vc is open,  Add to RX Queue
                  else dmaRxBuffer(desc,buff);

                  // Unlock
                  spin_unlock(&dev->maskLock);
               }

               // Buffer was not found
               else dev_warn(dev->device,"Irq: Failed to locate RX descriptor 0x%.8x.\n",handle);
            }
         }
      }

      // Enable interrupts
      iowrite32(0x1,&(reg->intEnable));
      return(IRQ_HANDLED);
   }
   return(IRQ_NONE);
}


// Init card in top level Probe
void AxisG1_Init(struct DmaDevice *dev) {
   uint32_t x;

   struct AxisG1Reg *reg;
   reg = (struct AxisG1Reg *)dev->reg;

   // Set MAX RX                      
   iowrite32(dev->cfgSize,&(reg->maxRxSize));
                                      
   // Clear FIFOs                     
   iowrite32(0x1,&(reg->fifoClear)); 
   iowrite32(0x0,&(reg->fifoClear)); 

   // Enable rx and tx
   iowrite32(0x1,&(reg->rxEnable));
   iowrite32(0x1,&(reg->txEnable));

   // Push RX buffers to hardware
   for (x=0; x < dev->rxBuffers.count; x++) {
      if ( dmaBufferToHw(dev->rxBuffers.indexed[x]) < 0 ) 
         dev_warn(dev->device,"Init: Failed to map dma buffer.\n");
      else iowrite32(dev->rxBuffers.indexed[x]->buffHandle,&(reg->rxFree));
   }

   // Enable interrupt
   iowrite32(0x1,&(reg->intPendAck));
   iowrite32(0x1,&(reg->intEnable));

   // Online bits = 1, Ack bit = 0
   iowrite32(0x1,&(reg->onlineAck));

   // Set dest mask
   memset(dev->destMask,0xFF,DMA_MASK_SIZE);
   dev_info(dev->device,"Init: Found Version 1 Device.\n");
}


// Clear card in top level Remove
void AxisG1_Clear(struct DmaDevice *dev) {
   struct AxisG1Reg *reg;
   reg = (struct AxisG1Reg *)dev->reg;

   // Disable interrupt
   iowrite32(0x0,&(reg->intEnable));

   // Clear FIFOs
   iowrite32(0x1,&(reg->fifoClear));

   // Disable rx and tx
   iowrite32(0x0,&(reg->rxEnable));
   iowrite32(0x0,&(reg->txEnable));
   iowrite32(0x0,&(reg->onlineAck));
}


// Return receive buffer to card
// Single write so we don't need to lock
void AxisG1_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer *buff) {
   struct AxisG1Reg *reg;
   reg = (struct AxisG1Reg *)dev->reg;

   if ( dmaBufferToHw(buff) < 0 )
      dev_warn(dev->device,"RetRxBuffer: Failed to map dma buffer.\n");
   else iowrite32(buff->buffHandle,&(reg->rxFree));
}


// Send a buffer
int32_t AxisG1_SendBuffer(struct DmaDevice *dev, struct DmaBuffer *buff) {
   uint32_t control;

   struct AxisG1Reg *reg;
   reg = (struct AxisG1Reg *)dev->reg;

   // Create descriptor
   control  = (buff->dest       ) & 0x000000FF;
   control += (buff->flags <<  8) & 0x00FFFF00; // flags[15:9] = luser, flags[7:0] = fuser

   if ( dmaBufferToHw(buff) < 0 ) {
      dev_warn(dev->device,"SendBuffer: Failed to map dma buffer.\n");
      return(-1);
   }

   // Write to hardware
   spin_lock(&dev->writeHwLock);

   iowrite32(buff->buffHandle,&(reg->txPostA));
   iowrite32(buff->size,&(reg->txPostB));
   iowrite32(control,&(reg->txPostC));

   spin_unlock(&dev->writeHwLock);

   return(buff->size);
}


// Execute command
int32_t AxisG1_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   struct AxisG1Reg *reg;
   reg = (struct AxisG1Reg *)dev->reg;

   switch (cmd) {

      // Read ACK
      case AXIS_Read_Ack:
         spin_lock(&dev->commandLock);
         iowrite32(0x3,&(reg->onlineAck));
         iowrite32(0x1,&(reg->onlineAck));
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
void AxisG1_SeqShow(struct seq_file *s, struct DmaDevice *dev) {
   struct AxisG1Reg *reg;
   reg = (struct AxisG1Reg *)dev->reg;

   seq_printf(s,"\n");
   seq_printf(s,"-------------- General HW -----------------\n");
   seq_printf(s,"             Writable : %i\n",((ioread32(&(reg->fifoValid)) >> 1) & 0x1));
   seq_printf(s,"             Readable : %i\n",(ioread32(&(reg->fifoValid)) & 0x1));
   seq_printf(s,"     Write Int Status : %i\n",((ioread32(&(reg->intPendAck)) >> 1) & 0x1));
   seq_printf(s,"      Read Int Status : %i\n",(ioread32(&(reg->intPendAck)) & 0x1));
}

