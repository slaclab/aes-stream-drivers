/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    This module provides an interface and management functions for handling
 *    DMA operations on Axis Generation 2 devices. It includes mechanisms for buffer
 *    management, IRQ handling, and command execution, enabling efficient data
 *    transfer and control operations between the host and the Axis Gen2 hardware.
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

/**
 * struct hardware_functions - Hardware function pointers for AXIS Gen2 card.
 *
 * This structure provides a set of function pointers for handling various
 * hardware operations specific to the AXIS Gen2 card. It is part of the device
 * driver implementation, enabling abstraction and polymorphism for different
 * hardware implementations.
 *
 * @irq: Pointer to the function handling interrupts.
 * @init: Pointer to the initialization function.
 * @enable: Pointer to the function that enables the device.
 * @clear: Pointer to the function that clears device state or buffers.
 * @retRxBuffer: Pointer to the function that returns a received buffer.
 * @sendBuffer: Pointer to the function for sending a buffer.
 * @command: Pointer to the function that handles device-specific commands.
 * @seqShow: Pointer to the function that supports the seq_file interface for device status reporting.
 */
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

/**
 * AxisG2_MapReturn - Map return descriptor
 * @dev: Pointer to the device structure.
 * @ret: Pointer to the return structure to be mapped.
 * @desc128En: Descriptor size enable flag (1 for 128-bit, 0 for 64-bit).
 * @index: Index of the descriptor to be mapped.
 * @ring: Pointer to the ring buffer.
 *
 * This inline function maps a return descriptor for processing, handling the
 * mapping of return descriptors from the DMA engine based on the descriptor
 * size indicated by @desc128En. It updates the @ret structure with the
 * descriptor's information.
 *
 * Return: Status of the mapping (0 for failure, 1 for success).
 */
inline uint8_t AxisG2_MapReturn(struct DmaDevice * dev, struct AxisG2Return *ret, uint32_t desc128En, uint32_t index, uint32_t *ring) {
   uint32_t * ptr;
   uint32_t chan;
   uint32_t dest;

   // Calculate pointer to the descriptor based on index and descriptor size
   ptr = (ring + (index*(desc128En?4:2)));

   if ( desc128En ) {
      // For 128-bit descriptors
      if ( ptr[3] == 0 ) return 0;

      chan        = (ptr[3] >> 8) & 0xF;
      dest        = (ptr[3] & 0xFF);
      ret->dest   = (chan * 256) + dest;
      ret->size   = ptr[2];
      ret->index  = ptr[1];
      ret->fuser  = (ptr[0] >> 24) & 0xFF;
      ret->luser  = (ptr[0] >> 16) & 0xFF;
      ret->id     = (ptr[0] >>  8) & 0xFF;
      ret->cont   = (ptr[0] >>  3) & 0x1;
      ret->result = ptr[0] & 0x7;

   } else {
      // For 64-bit descriptors
      if ( ptr[1] == 0 ) return 0;

      ret->dest   = (ptr[1] >> 24) & 0xFF;
      ret->size   = (ptr[1] & 0xFFFFFF);
      ret->fuser  = (ptr[0] >> 24) & 0xFF;
      ret->luser  = (ptr[0] >> 16) & 0xFF;
      ret->index  = (ptr[0] >>  4) & 0xFFF;
      ret->cont   = (ptr[0] >>  3) & 0x1;
      ret->result = ptr[0] & 0x7;
      ret->id     = 0;  // ID is set to 0 for 64-bit descriptors
   }

   // Logging for debug purposes
   if ( dev->debug > 0 )
      dev_info(dev->device,"MapReturn: desc idx %i, raw 0x%x, 0x%x, 0x%x, 0x%x\n",index,ptr[0],ptr[1],ptr[2],ptr[3]);

   // Clear the processed descriptor area
   memset(ptr,0,(desc128En?16:8));
   return 1;
}

/**
 * AxisG2_WriteFree - Add buffer to free list
 * @buff: Buffer to be added to the free list.
 * @reg: Pointer to the device register structure.
 * @desc128En: Descriptor size enable flag.
 *
 * Adds a buffer to the free list, making it available for use again.
 * This function manages the addition of buffers back to the device's pool
 * of free buffers. It writes the buffer index and, if enabled, the buffer handle
 * to the device's write FIFOs to mark the buffer as free.
 */
inline void AxisG2_WriteFree(struct DmaBuffer *buff, struct AxisG2Reg *reg, uint32_t desc128En) {
   uint32_t wrData[2];

   // Mask the buffer index to fit within the 28-bit field
   wrData[0] = buff->index & 0x0FFFFFFF;

   if (desc128En) {
      // If using 128-bit descriptors, encode the buffer handle across two 32-bit writes
      // First part: Addr bits 7:4 into the upper 4 bits of the first word
      wrData[0] |= (buff->buffHandle << 24) & 0xF0000000;
      // Second part: Addr bits 39:8 into the entirety of the second word
      wrData[1]  = (buff->buffHandle >>  8) & 0xFFFFFFFF;

      // Write the second part to the device's write FIFO B
      writel(wrData[1], &(reg->writeFifoB));

   // If not using 128-bit descriptors, write the buffer handle directly
   // to the device's DMA address table based on the buffer index
   } else {
      // For 64-bit descriptors
      writel(buff->buffHandle, &(reg->dmaAddr[buff->index]));
   }

   // Write the first part (or the entire buffer index for 32-bit descriptors)
   // to the device's write FIFO A
   writel(wrData[0], &(reg->writeFifoA));
}

/**
 * AxisG2_WriteTx - Add buffer to TX list
 * @buff: Buffer to be transmitted.
 * @reg: Pointer to the device register structure.
 * @desc128En: Descriptor size enable flag.
 *
 * Adds a buffer to the transmission list, making it ready for sending
 * out. This function configures the buffer's metadata and submits it to
 * the appropriate FIFO for transmission based on the descriptor size
 * enabled by the `desc128En` flag.
 */
inline void AxisG2_WriteTx(struct DmaBuffer *buff, struct AxisG2Reg *reg, uint32_t desc128En) {
   uint32_t rdData[4];
   uint32_t dest;
   uint32_t chan;

   // Configure buffer flags for transmission
   rdData[0]  = (buff->flags >> 13) & 0x00000008;  // bit[3] = continue = flags[16]
   rdData[0] |= (buff->flags <<  8) & 0x00FF0000;  // Bits[23:16] = lastUser = flags[15:8]
   rdData[0] |= (buff->flags << 24) & 0xFF000000;  // Bits[31:24] = firstUser = flags[7:0]

   if (desc128En) {
      // For 128-bit descriptor enabled
      dest = buff->dest % 256;
      chan = buff->dest / 256;

      rdData[0] |= (chan << 4) & 0x000000F0;  // Channel number
      rdData[0] |= (dest << 8) & 0x0000FF00;  // Destination ID

      rdData[1] = buff->size;  // Buffer size

      // Buffer index and handle for 128-bit descriptor
      rdData[2] = buff->index & 0x0FFFFFFF;
      rdData[2] |= (buff->buffHandle << 24) & 0xF0000000;  // Addr bits[31:28]
      rdData[3]  = (buff->buffHandle >>  8) & 0xFFFFFFFF;  // Addr bits[39:8]

      // Write to FIFO registers for 128-bit descriptor
      writel(rdData[3], &(reg->readFifoD));
      writel(rdData[2], &(reg->readFifoC));
   } else {
      // For 64-bit descriptors
      rdData[0] |= (buff->index <<  4) & 0x0000FFF0;  // Buffer ID

      rdData[1]  = buff->size & 0x00FFFFFF;  // Buffer size
      rdData[1] |= (buff->dest << 24) & 0xFF000000;  // Destination ID

      // Write buffer handle to DMA address table
      writel(buff->buffHandle, &(reg->dmaAddr[buff->index]));
   }

   // Write to FIFO registers
   writel(rdData[1], &(reg->readFifoB));
   writel(rdData[0], &(reg->readFifoA));
}

/**
 * AxisG2_Process - Process receive and transmit data
 * @dev: Pointer to the device structure
 * @reg: Pointer to the device register structure
 * @hwData: Hardware data to be processed
 *
 * This function processes both received and to be transmitted data, handling
 * the data movement from and to the hardware, and managing both the receive
 * and transmit queues.
 *
 * Returns: Number of processed items
 */
uint32_t AxisG2_Process(struct DmaDevice * dev, struct AxisG2Reg *reg, struct AxisG2Data *hwData) {
   struct DmaDesc *desc;
   struct DmaBuffer *buff;
   struct AxisG2Return ret;

   uint32_t x;
   uint32_t bCnt;
   uint32_t rCnt;
   uint32_t handleCount;

   handleCount = 0;
   ////////////////// Transmit Buffers /////////////////////////

   // Check read (transmit) returns
   while ( AxisG2_MapReturn(dev,&ret,hwData->desc128En,hwData->readIndex,hwData->readAddr) ) {
      ++handleCount;
      --(hwData->hwRdBuffCnt);

      if ( dev->debug > 0 ) dev_info(dev->device,"Process: Got TX Descriptor: Idx=%i, Pos=%i\n",ret.index,hwData->readIndex);

      // Attempt to find buffer in tx pool and return. otherwise return rx entry to hw.
      // Must adjust counters here and check for buffer need
      if ((buff = dmaRetBufferIdxIrq(dev,ret.index)) != NULL) {
         // Add to receive/write software queue
         if ( hwData->hwWrBuffCnt >= (hwData->addrCount-1) ) {
            dmaQueuePushIrq(&(hwData->wrQueue),buff);
         } else {
            // Add directly to receive/write hardware queue
            ++(hwData->hwWrBuffCnt);
            AxisG2_WriteFree(buff,reg,hwData->desc128En);
         }
      }
      hwData->readIndex = ((hwData->readIndex+1) % hwData->addrCount);
   }

   // Process transmit software queue
   if ( hwData->desc128En ) {
      while ( (hwData->hwRdBuffCnt < (hwData->addrCount-1)) && ((buff = dmaQueuePopIrq(&(hwData->rdQueue))) != NULL) ) {
         // Write to hardware
         AxisG2_WriteTx(buff,reg,hwData->desc128En);
         ++hwData->hwRdBuffCnt;
      }
   }

   ////////////////// Receive Buffers /////////////////////////

   // Lock to protect shared resources
   spin_lock(&dev->maskLock);

   // Check write (receive) descriptors
   while ( AxisG2_MapReturn(dev,&ret,hwData->desc128En,hwData->writeIndex,hwData->writeAddr) ) {
      ++handleCount;
      --(hwData->hwWrBuffCnt);

      if ( dev->debug > 0 ) dev_info(dev->device,"Process: Got RX Descriptor: Idx=%i, Pos=%i\n",ret.index,hwData->writeIndex);

      if ( (buff = dmaGetBufferList(&(dev->rxBuffers),ret.index)) != NULL ) {
         // Set buffer properties based on descriptor info
         buff->count++;

         buff->size  = ret.size;
         buff->dest  = ret.dest;
         buff->error = (ret.size == 0)?DMA_ERR_FIFO:ret.result;
         buff->id    = ret.id;

         buff->flags =  ret.fuser;                      // firstUser = flags[7:0]
         buff->flags |= (ret.luser << 8) & 0x0000FF00;  // lastUser = flags[15:8]
         buff->flags |= (ret.cont << 16) & 0x00010000;  // continue = flags[16]

         hwData->contCount += ret.cont;

         if ( dev->debug > 0 ) {
            dev_info(dev->device,"Process: Rx size=%i, Dest=0x%x, fuser=0x%x, luser=0x%x, cont=%i, Error=0x%x\n",
               ret.size, ret.dest, ret.fuser, ret.luser, ret.cont, buff->error);
         }

         // Determine the owner of the buffer based on dest
         if (buff->dest < DMA_MAX_DEST) {
            desc = dev->desc[buff->dest];
         } else {
            desc = NULL;
         }

         // Return entry to FPGA if descriptor is not open
         if ( desc == NULL ) {
            if ( dev->debug > 0 ) dev_info(dev->device,"Process: Port not open return to free list.\n");

            if (hwData->hwWrBuffCnt < (hwData->addrCount-1)) {
               AxisG2_WriteFree(buff,reg,hwData->desc128En);
               ++hwData->hwWrBuffCnt;
            }
            else dmaQueuePushIrq(&(hwData->wrQueue),buff);

            // Background operation handling
            if ( (hwData->bgEnable >> buff->id) & 0x1 ) {
               writel(0x1,&(reg->bgCount[buff->id]));
            }
         }

         // Lane/VC is open; add to RX queue
         else dmaRxBufferIrq(desc,buff);
      }
      else dev_warn(dev->device,"Process: Failed to locate RX buffer index %i.\n", ret.index);

      // Update write index
      hwData->writeIndex = ((hwData->writeIndex+1) % hwData->addrCount);
   }

   // Release the lock
   spin_unlock(&dev->maskLock);

   // Get (write / receive) return buffer list and process
   if ( hwData->desc128En ) {
      do {
         rCnt = ((hwData->addrCount-1) - hwData->hwWrBuffCnt);
         if (rCnt > BUFF_LIST_SIZE ) rCnt = BUFF_LIST_SIZE;
         bCnt = dmaQueuePopListIrq(&(hwData->wrQueue),hwData->buffList,rCnt);
         for (x=0; x < bCnt; x++) {
            AxisG2_WriteFree(hwData->buffList[x],reg,hwData->desc128En);
            ++hwData->hwWrBuffCnt;
         }
      } while (bCnt > 0);
   }

   return handleCount;
}

/**
 * AxisG2_Irq - Interrupt handler for AXIS Gen2 DMA.
 * @irq: Interrupt request number.
 * @dev_id: Pointer to device-specific data.
 *
 * This function is invoked when the AXIS Gen2 DMA device triggers an interrupt.
 * It disables further interrupts, logs the interrupt occurrence if debugging is enabled,
 * and schedules work to handle the data if appropriate.
 *
 * Return:
 * IRQ_HANDLED - Indicates that the interrupt was successfully handled.
 */
irqreturn_t AxisG2_Irq(int irq, void *dev_id) {
   struct DmaDevice *dev;
   struct AxisG2Reg *reg;
   struct AxisG2Data *hwData;

   dev = (struct DmaDevice *)dev_id;
   reg = (struct AxisG2Reg *)dev->reg;
   hwData = (struct AxisG2Data *)dev->hwData;

   // Disable interrupt
   writel(0x0, &(reg->intEnable));

   // Log interrupt occurrence if debugging is enabled
   if (dev->debug > 0) {
      dev_info(dev->device, "Irq: Called.\n");
   }

   // Schedule work to handle the data if not disabled and work queue is enabled
   if ((!dev->cfgIrqDis) && hwData->wqEnable) {
      queue_work(hwData->wq, &(hwData->irqWork));
   }

   return IRQ_HANDLED;
}

/**
 * AxisG2_Init - Initialize Axis G2 DMA device
 * @dev: Pointer to the DmaDevice structure representing the device.
 *
 * This function initializes the Axis G2 DMA device during the probe phase. It sets up
 * necessary resources, configurations, and software structures for device operation.
 * This includes setting up DMA buffers, configuring hardware registers, and initializing
 * software queues for efficient DMA transfers.
 */
void AxisG2_Init(struct DmaDevice *dev) {
   uint32_t x;
   uint32_t size;

   struct DmaBuffer  *buff;
   struct AxisG2Data *hwData;
   struct AxisG2Reg  *reg;

   // Map device registers for access
   reg = (struct AxisG2Reg *)dev->reg;

   // Initialize destination mask to all 1's
   memset(dev->destMask,0xFF,DMA_MASK_SIZE);

   // Allocate and initialize hardware data structure
   hwData = (struct AxisG2Data *)kzalloc(sizeof(struct AxisG2Data),GFP_KERNEL);
   dev->hwData = hwData;
   hwData->dev = dev;

   // Determine operation mode (64-bit or 128-bit) based on hardware version
   hwData->desc128En = ((readl(&(reg->enableVer)) & 0x10000) != 0);

   // Initialize buffer counters
   hwData->hwWrBuffCnt = 0;
   hwData->hwRdBuffCnt = 0;

   // Initialize software queues if in 128-bit descriptor mode
   if ( hwData->desc128En ) {
      dmaQueueInit(&hwData->wrQueue,dev->rxBuffers.count);
      dmaQueueInit(&hwData->rdQueue,dev->txBuffers.count + dev->rxBuffers.count);
      hwData->buffList = (struct DmaBuffer **)kzalloc(BUFF_LIST_SIZE * sizeof(struct DmaBuffer *),GFP_ATOMIC);
   }

   // Calculate and set the addressable space based on register settings
   hwData->addrCount = (1 << readl(&(reg->addrWidth)));
   size = hwData->addrCount*(hwData->desc128En?16:8);

   // Allocate DMA buffers based on configuration mode
   if (dev->cfgMode & AXIS2_RING_ACP) {
      // Allocate read and write buffers in contiguous physical memory
      hwData->readAddr   = kzalloc(size, GFP_DMA | GFP_KERNEL);
      hwData->readHandle = virt_to_phys(hwData->readAddr);
      hwData->writeAddr   = kzalloc(size, GFP_DMA | GFP_KERNEL);
      hwData->writeHandle = virt_to_phys(hwData->writeAddr);
   } else {
      // Allocate coherent DMA buffers for read and write operations
      hwData->readAddr = dma_alloc_coherent(dev->device, size, &(hwData->readHandle), GFP_DMA | GFP_KERNEL);
      hwData->writeAddr = dma_alloc_coherent(dev->device, size, &(hwData->writeHandle), GFP_DMA | GFP_KERNEL);
   }

   // Log buffer addresses
   dev_info(dev->device,"Init: Read  ring at: sw 0x%llx -> hw 0x%llx.\n",(uint64_t)hwData->readAddr,(uint64_t)hwData->readHandle);
   dev_info(dev->device,"Init: Write ring at: sw 0x%llx -> hw 0x%llx.\n",(uint64_t)hwData->writeAddr,(uint64_t)hwData->writeHandle);

   // Initialize read ring buffer addresses and indices
   writel(hwData->readHandle&0xFFFFFFFF,&(reg->rdBaseAddrLow));
   writel((hwData->readHandle >> 32)&0xFFFFFFFF,&(reg->rdBaseAddrHigh));
   memset(hwData->readAddr,0,size);
   hwData->readIndex = 0;

   // Initialize write ring buffer addresses and indices
   writel(hwData->writeHandle&0xFFFFFFFF,&(reg->wrBaseAddrLow));
   writel((hwData->writeHandle>>32)&0xFFFFFFFF,&(reg->wrBaseAddrHigh));
   memset(hwData->writeAddr,0,size);
   hwData->writeIndex = 0;

   // Initialize interrupt and continuity counters
   hwData->missedIrq = 0;
   hwData->contCount = 0;

   // Configure cache mode based on device configuration:
   // bits3:0 = descWr, bits 11:8 = bufferWr, bits 15:12 = bufferRd
   x = 0;
   if (dev->cfgMode & BUFF_ARM_ACP) x |= 0xA600;  // Buffer write and read cache policy
   if (dev->cfgMode & AXIS2_RING_ACP) x |= 0x00A6;  // Descriptor write cache policy
   writel(x, &(reg->cacheConfig));

   // Set maximum transfer size
   writel(dev->cfgSize,&(reg->maxSize));

   // Reset FIFOs to clear any residual data
   writel(0x1,&(reg->fifoReset));
   writel(0x0,&(reg->fifoReset));

   // Enable continuous mode and disable drop mode
   writel(0x1,&(reg->contEnable));
   writel(0x0,&(reg->dropEnable));

   // Set IRQ holdoff time if supported by hardware version
   if ( ((readl(&(reg->enableVer)) >> 24) & 0xFF) >= 3 ) writel(dev->cfgIrqHold,&(reg->irqHoldOff));

   // Push RX buffers to hardware and map
   for (x=dev->rxBuffers.baseIdx; x < (dev->rxBuffers.baseIdx + dev->rxBuffers.count); x++) {
      buff = dmaGetBufferList(&(dev->rxBuffers),x);

      // Map failure
      if ( dmaBufferToHw(buff) < 0 ) {
          dev_warn(dev->device,"Init: Failed to map dma buffer.\n");

      // Add to software queue, if enabled and hardware is full
      } else if ( hwData->desc128En && (hwData->hwWrBuffCnt >= (hwData->addrCount-1)) ) {
         dmaQueuePush(&(hwData->wrQueue),buff);

      // Add to hardware queue
      } else {
         ++hwData->hwWrBuffCnt;
         AxisG2_WriteFree(buff,reg,hwData->desc128En);
      }
   }

   // Initialize buffer group settings if supported by hardware version
   hwData->bgEnable = 0;
   if ( ((readl(&(reg->enableVer)) >> 24) & 0xFF) >= 4 ) {
      for (x =0; x < 8; x++) {
         if ( dev->cfgBgThold[x] != 0 ) hwData->bgEnable |= (1 << x);
         writel(dev->cfgBgThold[x],&(reg->bgThold[x]));
      }
   }

   dev_info(dev->device,"Init: Found Version 2 Device. Desc128En=%i\n",hwData->desc128En);
}

/**
 * AxisG2_Enable - Enable the AXIS Gen2 device
 * @dev: Pointer to the device structure.
 *
 * This function enables the AXIS Gen2 device, making it ready for operation.
 * It is typically called after the device has been initialized to start its
 * functionality. The function configures the device's hardware registers to
 * enable the device and its interrupt handling. It also initializes and starts
 * a workqueue if required, based on the device's configuration.
 */
void AxisG2_Enable(struct DmaDevice *dev) {
   struct AxisG2Reg  *reg;
   struct AxisG2Data *hwData;

   reg = (struct AxisG2Reg *)dev->reg;
   hwData = (struct AxisG2Data *)dev->hwData;

   // Enable the device by setting the enable version and online registers
   writel(0x1, &(reg->enableVer));
   writel(0x1, &(reg->online));

   // Check if descriptor 128-bit enable flag is set
   if (hwData->desc128En) {
      hwData->wqEnable = 1;

      // Configure workqueue and delayed work for interrupt handling or polling
      if (!dev->cfgIrqDis) {
         // Create a single-thread workqueue for interrupt handling
         hwData->wq = create_singlethread_workqueue("AXIS_G2_WORKQ");
         INIT_DELAYED_WORK(&(hwData->dlyWork), AxisG2_WqTask_IrqForce);
         queue_delayed_work(hwData->wq, &(hwData->dlyWork), 10);

         // Initialize work for processing, called from IRQ
         INIT_WORK(&(hwData->irqWork), AxisG2_WqTask_Service);
      } else {
         // Allocate workqueue for polling mode, without interrupts
         hwData->wq = alloc_workqueue("%s", WQ_MEM_RECLAIM | WQ_SYSFS, 1, "AXIS_G2_WORKQ");
         INIT_WORK(&(hwData->irqWork), AxisG2_WqTask_Poll);
         queue_work_on(dev->cfgIrqDis, hwData->wq, &(hwData->irqWork));
      }
   } else {
      hwData->wqEnable = 0;
   }

   // Enable interrupt handling if not disabled by configuration
   if (!dev->cfgIrqDis) {
      writel(0x1, &(reg->intEnable));
   }

   // Re-enable the device and online status to ensure settings take effect
   writel(0x1, &(reg->enableVer));
   writel(0x1, &(reg->online));

   // Re-enable interrupt handling as a final step
   writel(0x1, &(reg->intEnable));
}

/**
 * AxisG2_Clear - Clear device resources
 * @dev: Pointer to the device structure.
 *
 * This function clears and releases the device resources during the removal
 * phase. It ensures that the device's interrupts are disabled, the work queue
 * is stopped and destroyed, RX and TX are disabled, FIFOs are cleared, and
 * all allocated memory for buffers is freed, thus properly shutting down and
 * cleaning up the device.
 */
void AxisG2_Clear(struct DmaDevice *dev) {
   struct AxisG2Reg *reg;
   struct AxisG2Data *hwData;
   size_t size;

   reg = (struct AxisG2Reg *)dev->reg;
   hwData = (struct AxisG2Data *)dev->hwData;

   // Disable interrupts to prevent further device activity.
   writel(0x0, &(reg->intEnable));

   // Stop and destroy the work queue if enabled.
   if (hwData->wqEnable) {
      hwData->wqEnable = 0;
      // Cancel any pending delayed work if IRQs are not disabled.
      if (!dev->cfgIrqDis) {
         cancel_delayed_work_sync(&(hwData->dlyWork));
      }
      // Ensure all work for the device is completed before freeing resources.
      flush_workqueue(hwData->wq);
      destroy_workqueue(hwData->wq);
   }

   // Disable RX and TX to stop data transfers.
   writel(0x0, &(reg->enableVer));
   writel(0x0, &(reg->online));

   // Clear FIFOs to reset the device's internal state.
   writel(0x1, &(reg->fifoReset));

   // Free allocated buffers depending on the device configuration.
   if (dev->cfgMode & AXIS2_RING_ACP) {
      // For AXIS2_RING_ACP mode, use kfree for buffer deallocation.
      kfree(hwData->readAddr);
      kfree(hwData->writeAddr);
   } else {
      // Compute real DMA size. This must match what was passed into dma_alloc_coherent
      size = hwData->addrCount * (hwData->desc128En ? 16 : 8);

      // For non-ACP modes, use dma_free_coherent to ensure proper DMA memory management.
      dma_free_coherent(dev->device, size, hwData->writeAddr, hwData->writeHandle);
      dma_free_coherent(dev->device, size, hwData->readAddr, hwData->readHandle);
   }

   // Free the buffer list if descriptor 128-bit mode is enabled.
   if (hwData->desc128En) {
      kfree(hwData->buffList);
   }

   // Finally, free the hardware data structure itself.
   kfree(hwData);
}

/**
 * AxisG2_RetRxBuffer - Return receive buffers to card
 * @dev:   Pointer to the device structure.
 * @buff:  Buffers to be returned.
 * @count: Number of buffers to be returned.
 *
 * This function returns received buffers back to the card for reuse,
 * which is typically invoked after the application has processed the received data.
 * It handles both 64-bit and 128-bit descriptor modes, ensuring buffers are
 * correctly returned to the hardware for future use. Errors in buffer mapping are
 * reported, and the function supports background operation mode for buffer group
 * credits, triggering an interrupt after pushing buffers to the software queue
 * in 128-bit descriptor mode.
 */
void AxisG2_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer **buff, uint32_t count) {
   struct AxisG2Reg *reg;
   struct AxisG2Data *hwData;
   uint32_t x;

   reg = (struct AxisG2Reg *)dev->reg;
   hwData = (struct AxisG2Data *)dev->hwData;

   // Prepare for hardware interaction
   for (x = 0; x < count; x++) {
      if (dmaBufferToHw(buff[x]) < 0) {
         dev_warn(dev->device, "RetRxBuffer: Failed to map dma buffer.\n");
         return;
      }

      // Directly write to hardware for 64-bit descriptors, no locking needed
      if (!hwData->desc128En) {
         AxisG2_WriteFree(buff[x], reg, hwData->desc128En);
      }
   }

   // For 128-bit descriptors, push to software queue and force an interrupt
   if (hwData->desc128En) {
      dmaQueuePushList(&(hwData->wrQueue), buff, count);

      // Handle buffer group credits if background operation is enabled
      if (hwData->bgEnable != 0) {
         for (x = 0; x < count; x++) {
            if ((hwData->bgEnable >> buff[x]->id) & 0x1) {
               writel(0x1, &(reg->bgCount[buff[x]->id]));
            }
         }
      }

      // Force an interrupt to process the returned buffers
      writel(0x1, &(reg->forceInt));
   }
}

/**
 * AxisG2_SendBuffer - Send a buffer or a series of buffers.
 * @dev:   Pointer to the device structure.
 * @buff:  Buffer to be sent.
 * @count: Number of buffers to be sent.
 *
 * This function sends out a buffer or a series of buffers, queuing them for transmission
 * through the DMA engine. It supports both 64-bit and 128-bit descriptor modes. In 64-bit
 * mode, buffers are written directly to the hardware, while in 128-bit mode, buffers are
 * pushed to a software queue and an interrupt is forced to handle the transfer.
 *
 * Return: On success, the number of buffers queued for transmission. On error, -1 if
 *         a buffer mapping to hardware fails.
 */
int32_t AxisG2_SendBuffer(struct DmaDevice *dev, struct DmaBuffer **buff, uint32_t count) {
   struct AxisG2Data *hwData;
   struct AxisG2Reg *reg;
   unsigned long iflags;
   uint32_t x;

   reg = (struct AxisG2Reg *)dev->reg;
   hwData = (struct AxisG2Data *)dev->hwData;

   // Prepare buffers for hardware transmission
   for (x = 0; x < count; x++) {
      if (dmaBufferToHw(buff[x]) < 0) {
         dev_warn(dev->device, "SendBuffer: Failed to map dma buffer.\n");
         return -1;
      }

      // Direct hardware write for 64-bit descriptors
      if (!hwData->desc128En) {
         spin_lock_irqsave(&dev->writeHwLock, iflags);
         AxisG2_WriteTx(buff[x], reg, hwData->desc128En);
         spin_unlock_irqrestore(&dev->writeHwLock, iflags);
      }
   }

   // For 128-bit descriptors, push to software queue and force an interrupt
   if (hwData->desc128En) {
      dmaQueuePushList(&(hwData->rdQueue), buff, count);
      writel(0x1, &(reg->forceInt));
   }

   return count;
}

/**
 *---------------------------------------------------------------------------
 * AxisG2_Command - Execute device command
 * @dev: Pointer to the device structure.
 * @cmd: Command to be executed.
 * @arg: Argument for the command.
 *
 * Executes a specific command on the device, providing a mechanism for
 * controlling device behavior or querying device status.
 *
 * Return: Status of the command execution.
 *---------------------------------------------------------------------------
 */
int32_t AxisG2_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   struct AxisG2Reg *reg;
   reg = (struct AxisG2Reg *)dev->reg;

   switch (cmd) {
      case AXIS_Read_Ack:
         // Lock the device command execution context
         spin_lock(&dev->commandLock);
         // Acknowledge the read request
         writel(0x1, &(reg->acknowledge));
         // Unlock after command execution
         spin_unlock(&dev->commandLock);
         return 0;
         break;

      case AXIS_Write_ReqMissed:
         // Return the number of missed write requests
         return readl(&(reg->wrReqMissed));
         break;

      default:
         // Log a warning for an invalid command and return an error
         dev_warn(dev->device, "Command: Invalid command=%i\n", cmd);
         return -1;
         break;
   }
}

/**
 * AxisG2_SeqShow - Add data to proc dump.
 * @s: Sequence file pointer.
 * @dev: Pointer to the device structure.
 *
 * This function adds device-specific data to the proc file system dump,
 * enabling debugging and status queries via the /proc file system. It
 * iterates through various DMA firmware and hardware state parameters,
 * formatting and printing them to the sequence file for easy access
 * and readability.
 */
void AxisG2_SeqShow(struct seq_file *s, struct DmaDevice *dev) {
   struct AxisG2Reg *reg;
   struct AxisG2Data *hwData;
   uint32_t x;

   reg = (struct AxisG2Reg *)dev->reg;
   hwData = (struct AxisG2Data *)dev->hwData;

   seq_printf(s,"\n");
   seq_printf(s,"---------- DMA Firmware General ----------\n");
   seq_printf(s,"          Int Req Count : %u\n",(readl(&(reg->intReqCount))));
// seq_printf(s,"        Hw Dma Wr Index : %u\n",(readl(&(reg->hwWrIndex))));
// seq_printf(s,"        Sw Dma Wr Index : %u\n",hwData->writeIndex);
// seq_printf(s,"        Hw Dma Rd Index : %u\n",(readl(&(reg->hwRdIndex))));
// seq_printf(s,"        Sw Dma Rd Index : %u\n",hwData->readIndex);
// seq_printf(s,"     Missed Wr Requests : %u\n",(readl(&(reg->wrReqMissed))));
// seq_printf(s,"       Missed IRQ Count : %u\n",hwData->missedIrq);
   seq_printf(s,"         Continue Count : %u\n",hwData->contCount);
   seq_printf(s,"          Address Count : %i\n",hwData->addrCount);
   seq_printf(s,"    Hw Write Buff Count : %i\n",hwData->hwWrBuffCnt);
   seq_printf(s,"     Hw Read Buff Count : %i\n",hwData->hwRdBuffCnt);
   seq_printf(s,"           Cache Config : 0x%x\n",(readl(&(reg->cacheConfig))));
   seq_printf(s,"            Desc 128 En : %i\n",hwData->desc128En);
   seq_printf(s,"            Enable Ver  : 0x%x\n",(readl(&(reg->enableVer))));
   seq_printf(s,"      Driver Load Count : %u\n",((readl(&(reg->enableVer)))>>8)&0xFF);
   seq_printf(s,"               IRQ Hold : %u\n",(readl(&(reg->irqHoldOff))));
   seq_printf(s,"              BG Enable : 0x%x\n",hwData->bgEnable);

   for ( x=0; x < 8; x++ ) {
      if ( (hwData->bgEnable >> x) & 0x1 ) {
         seq_printf(s,"         BG %i Threshold : %u\n",x,readl(&(reg->bgThold[x])));
         seq_printf(s,"             BG %i Count : %u\n",x,readl(&(reg->bgCount[x])));
      }
   }
}

/**
 *-------------------------------------------------------------------------------
 * AxisG2_WqTask_IrqForce - Work queue task to force periodic IRQ
 * @work: Pointer to the work_struct structure.
 *
 * This function forces a periodic interrupt request (IRQ) to ensure that the
 * device remains responsive and to handle any conditions that require regular
 * attention. It is designed to be used as part of a work queue mechanism.
 *-------------------------------------------------------------------------------
 */
void AxisG2_WqTask_IrqForce(struct work_struct *work) {
   struct AxisG2Reg *reg;
   struct AxisG2Data *hwData;
   struct delayed_work *dlyWork;

   // Convert from work_struct to delayed_work
   dlyWork = container_of(work, struct delayed_work, work);
   // Get the container AxisG2Data
   hwData = container_of(dlyWork, struct AxisG2Data, dlyWork);

   // Access device registers
   reg = (struct AxisG2Reg *)hwData->dev->reg;

   // Force an interrupt
   writel(0x1, &(reg->forceInt));

   // If work queue is enabled, re-queue the work with a delay
   if (hwData->wqEnable)
      queue_delayed_work(hwData->wq, &(hwData->dlyWork), 10);
}

/**
 * AxisG2_WqTask_Poll - Work queue task for polling
 * @work: Work structure.
 *
 * Implements a polling mechanism as a work queue task. This function allows
 * for periodic checks of the device status or data without relying on interrupts.
 * It processes data through the AxisG2_Process function and logs the number of
 * handled items if debugging is enabled. If work queue processing is enabled,
 * it re-queues itself for continuous operation.
 */
void AxisG2_WqTask_Poll(struct work_struct *work) {
   uint32_t handleCount;
   struct AxisG2Reg *reg;
   struct DmaDevice *dev;
   struct AxisG2Data *hwData;

   // Extract hardware data from the work structure
   hwData = container_of(work, struct AxisG2Data, irqWork);

   // Retrieve device and register structures
   reg = (struct AxisG2Reg *)hwData->dev->reg;
   dev = (struct DmaDevice *)hwData->dev;

   // Process data and return the number of handled items
   handleCount = AxisG2_Process(dev, reg, hwData);

   // Log the number of handled items if debugging is enabled
   if (dev->debug > 0 && handleCount > 0) {
      dev_info(dev->device, "Poll: Done. Handled = %i\n", handleCount);
   }

   // Re-queue work if work queue processing is enabled
   if (hwData->wqEnable) {
      queue_work_on(dev->cfgIrqDis, hwData->wq, &(hwData->irqWork));
   }
}

/**
 * AxisG2_WqTask_Service - Work queue task for IRQ processing
 * @work: Work structure.
 *
 * This function handles IRQ processing as a work queue task, allowing for
 * deferred or asynchronous processing of interrupt-driven events. It is
 * designed to process data, handle interrupt acknowledgment, and manage
 * interrupt enablement through work queues.
 */
void AxisG2_WqTask_Service(struct work_struct *work) {
   uint32_t handleCount;
   struct AxisG2Reg *reg;
   struct DmaDevice *dev;
   struct AxisG2Data *hwData;

   // Obtain the hardware data from the work structure
   hwData = container_of(work, struct AxisG2Data, irqWork);

   // Cast device and register pointers from the hardware data
   reg = (struct AxisG2Reg *)hwData->dev->reg;
   dev = (struct DmaDevice *)hwData->dev;

   // Debug information: entering service routine
   if (dev->debug > 0) {
      dev_info(dev->device, "Service: Entered\n");
   }

   // Process incoming data and handle it accordingly
   handleCount = AxisG2_Process(dev, reg, hwData);

   // Increment missed IRQ counter if no handle was processed
   if (handleCount == 0) {
      hwData->missedIrq++;
   }

   // Debug information: completion of service routine
   if (dev->debug > 0) {
      dev_info(dev->device, "Service: Done. Handled = %i\n", handleCount);
   }

   // Acknowledge interrupt and enable next interrupt
   writel(0x30000 + handleCount, &(reg->intAckAndEnable));
}
