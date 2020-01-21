/**
 *-----------------------------------------------------------------------------
 * Title      : Common access functions, not card specific
 * ----------------------------------------------------------------------------
 * File       : dma_common.c
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * Common access functions, not card specific
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
#include <DmaDriver.h>
#include <dma_common.h>
#include <dma_buffer.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/slab.h>

// Define interface routines
struct file_operations DmaFunctions = {
   read:           Dma_Read,
   write:          Dma_Write,
   open:           Dma_Open,
   release:        Dma_Release,
   poll:           Dma_Poll,
   fasync:         Dma_Fasync,
   unlocked_ioctl: (void *)Dma_Ioctl,
   compat_ioctl:   (void *)Dma_Ioctl,
   mmap:           Dma_Mmap
};

// Setup proc file operations
static struct file_operations DmaProcOps = {
   .owner   = THIS_MODULE,
   .open    = Dma_ProcOpen,
   .read    = seq_read,
   .llseek  = seq_lseek,
   .release = seq_release
};

// Sequence operations
static struct seq_operations DmaSeqOps = {
   .start = Dma_SeqStart,
   .next  = Dma_SeqNext,
   .stop  = Dma_SeqStop,
   .show  = Dma_SeqShow
};

// Number of active devices
uint32_t gDmaDevCount;

// Global variable for the device class 
struct class * gCl;


// Devnode callback to set permissions of created devices
char *Dma_DevNode(struct device *dev, umode_t *mode){
   if ( mode != NULL ) *mode = 0666;
   return(NULL);
}

// Map address space in buffer
int Dma_MapReg ( struct DmaDevice *dev ) {
   if ( dev->base == NULL ) {
      dev_info(dev->device,"Init: Mapping Register space 0x%llx with size 0x%x.\n",dev->baseAddr,dev->baseSize);
      dev->base = ioremap_nocache(dev->baseAddr, dev->baseSize);
      if (! dev->base ) {
         dev_err(dev->device,"Init: Could not remap memory.\n");
         return -1;
      }
      dev->reg = dev->base;
      dev_info(dev->device,"Init: Mapped to 0x%llx.\n",(uint64_t)dev->base);

      // Hold memory region
      if ( request_mem_region(dev->baseAddr, dev->baseSize, dev->devName) == NULL ) {
         dev_err(dev->device,"Init: Memory in use.\n");
         return -1;
      }
   }
   return(0);
}

// Create and init device, called from top level probe function
int Dma_Init(struct DmaDevice *dev) {

   int32_t x;
   int32_t res;
   uint64_t tot;

   // Default debug disable
   dev->debug = 0;

   // Allocate device numbers for character device. 1 minor numer starting at 0
   res = alloc_chrdev_region(&(dev->devNum), 0, 1, dev->devName);
   if (res < 0) {
      dev_err(dev->device,"Init: Cannot register char device\n");
      return(-1);
   }

   // Create class struct if it does not already exist
   if (gCl == NULL) {
      dev_info(dev->device,"Init: Creating device class\n");
      if ((gCl = class_create(THIS_MODULE, dev->devName)) == NULL) {
         dev_err(dev->device,"Init: Failed to create device class\n");
         return(-1);
      }
      gCl->devnode = (void *)Dma_DevNode;
   }

   // Attempt to create the device
   if (device_create(gCl, NULL, dev->devNum, NULL, dev->devName) == NULL) {
      dev_err(dev->device,"Init: Failed to create device file\n");
      return -1;
   }

   // Init the device
   cdev_init(&(dev->charDev), &DmaFunctions);
   dev->major = MAJOR(dev->devNum);

   // Add the charactor device
   if (cdev_add(&(dev->charDev), dev->devNum, 1) == -1) {
      dev_err(dev->device,"Init: Failed to add device file.\n");
      return -1;
   }                                  

   // Setup /proc
   proc_create_data(dev->devName, 0, NULL, &DmaProcOps, dev);

   // Remap the I/O register block so that it can be safely accessed.
   if ( Dma_MapReg(dev) < 0 ) return(-1);

   // Init descriptors
   for (x=0; x < DMA_MAX_DEST; x++) dev->desc[x] = NULL;

   // Init locks
   spin_lock_init(&(dev->writeHwLock));
   spin_lock_init(&(dev->commandLock));
   spin_lock_init(&(dev->maskLock));

   // Create tx buffers
   dev_info(dev->device,"Init: Creating %i TX Buffers. Size=%i Bytes. Mode=%i.\n",
        dev->cfgTxCount,dev->cfgSize,dev->cfgMode);
   res = dmaAllocBuffers (dev, &(dev->txBuffers), dev->cfgTxCount, 0, DMA_TO_DEVICE );
   tot = res * dev->cfgSize;

   dev_info(dev->device,"Init: Created  %i out of %i TX Buffers. %llu Bytes.\n", res,dev->cfgTxCount,tot);

   // Bad buffer allocation
   if ( dev->cfgTxCount > 0 && res == 0 ) return(-1);

   // Init transmit queue
   dmaQueueInit(&(dev->tq),dev->txBuffers.count);

   // Populate transmit queue
   for (x=dev->txBuffers.baseIdx; x < (dev->txBuffers.baseIdx + dev->txBuffers.count); x++) 
      dmaQueuePush(&(dev->tq),dmaGetBufferList(&(dev->txBuffers),x));

   // Create rx buffers, bidirectional because rx buffers can be passed to tx
   dev_info(dev->device,"Init: Creating %i RX Buffers. Size=%i Bytes. Mode=%i.\n",
        dev->cfgRxCount,dev->cfgSize,dev->cfgMode);
   res = dmaAllocBuffers (dev, &(dev->rxBuffers), dev->cfgRxCount, dev->txBuffers.count, DMA_BIDIRECTIONAL);
   tot = res * dev->cfgSize;

   dev_info(dev->device,"Init: Created  %i out of %i RX Buffers. %llu Bytes.\n", res,dev->cfgRxCount,tot);

   // Bad buffer allocation
   if ( dev->cfgRxCount > 0 && res == 0 ) return(-1);

   // Call card specific init
   dev->hwFunc->init(dev);

   // Set interrupt
   if ( dev->irq != 0 ) {
      dev_info(dev->device,"Init: IRQ %d\n", dev->irq);
      res = request_irq( dev->irq, dev->hwFunc->irq, IRQF_SHARED, dev->devName, (void*)dev);

      // Result of request IRQ from OS.
      if (res < 0) {
         dev_err(dev->device,"Init: Unable to allocate IRQ.");
         return -1;
      }
   }

   // Enable card
   dev->hwFunc->enable(dev);

   // Init util commands
   dev->utilCommand = NULL;

   return 0;
}


// Cleanup device, Called from top level remove function
void  Dma_Clean(struct DmaDevice *dev) {
   uint32_t x;

   // Cleanup proc
   remove_proc_entry(dev->devName,NULL);
   cdev_del(&(dev->charDev));

   // Unregister Device Driver this is neccessary but it is causing a kernel crash on removal.
   if ( gCl != NULL ) device_destroy(gCl, dev->devNum);
   else dev_warn(dev->device,"Clean: gCl is already NULL.\n");

   unregister_chrdev_region(dev->devNum, 1);

   // Call card specific CLear
   dev->hwFunc->clear(dev);

   // CLear tx queue
   dmaQueueFree(&(dev->tq));

   // Free buffers
   dmaFreeBuffers (&(dev->txBuffers));
   dmaFreeBuffers (&(dev->rxBuffers));

   // Clear descriptors if they exist
   for (x=0; x < DMA_MAX_DEST; x++) dev->desc[x] = NULL;

   // Release memory region
   release_mem_region(dev->baseAddr, dev->baseSize);

   // Release IRQ
   if ( dev->irq != 0 ) free_irq(dev->irq, dev);

   // Unmap
   iounmap(dev->base);

   if (gDmaDevCount == 0 && gCl != NULL) {
      dev_info(dev->device,"Clean: Destroying device class\n");
   }

   memset(dev,0,sizeof(struct DmaDevice));

   if (gDmaDevCount == 0 && gCl != NULL) {
      class_destroy(gCl);
      gCl = NULL;
   }
}


// Open Returns 0 on success, error code on failure
int Dma_Open(struct inode *inode, struct file *filp) {
   struct DmaDevice * dev;
   struct DmaDesc   * desc;

   // Find device structure
   dev = container_of(inode->i_cdev, struct DmaDevice, charDev);

   // Init descriptor  
   desc = (struct DmaDesc *)kmalloc(sizeof(struct DmaDesc),GFP_KERNEL);
   memset(desc,0,sizeof(struct DmaDesc));
   dmaQueueInit(&(desc->q),dev->cfgRxCount);
   desc->async_queue = NULL;
   desc->dev = dev;

   // Store for later
   filp->private_data = desc;
   return 0;
}


// Dma_Release
// Called when the device is closed
// Returns 0 on success, error code on failure
int Dma_Release(struct inode *inode, struct file *filp) {
   struct DmaDesc   * desc;
   struct DmaDevice * dev;
   struct DmaBuffer * buff;

   unsigned long iflags;
   uint32_t x;
   uint32_t cnt;
   uint32_t destByte;
   uint32_t destBit;

   desc = (struct DmaDesc *)filp->private_data;
   dev  = desc->dev;

   // Make sure we can't receive data while adjusting mask flags
   spin_lock_irqsave(&dev->maskLock,iflags);

   // Clear pointers
   for (x=0; x < DMA_MAX_DEST; x++) {
      destByte = x / 8;
      destBit  = 1 << (x % 8);
      if ( (destBit & desc->destMask[destByte]) != 0 ) dev->desc[x] = NULL;
   }

   spin_unlock_irqrestore(&dev->maskLock,iflags);

   if (desc->async_queue) Dma_Fasync(-1,filp,0);

   // Release buffers
   cnt = 0;
   while ( (buff = dmaQueuePop(&(desc->q))) != NULL ) {
      dev->hwFunc->retRxBuffer(dev,&buff,1);
      cnt++;
   }
   if ( cnt > 0 ) dev_info(dev->device,"Release: Removed %i buffers from closed device.\n", cnt);

   // Find rx buffers still owned by descriptor 
   cnt = 0;
   for (x=dev->rxBuffers.baseIdx; x < (dev->rxBuffers.baseIdx + dev->rxBuffers.count); x++) {
      buff = dmaGetBufferList(&(dev->rxBuffers),x);

      if ( buff->userHas == desc ) {
         buff->userHas = NULL;
         dev->hwFunc->retRxBuffer(dev,&buff,1);
         cnt++;
      }
   }

   if ( cnt > 0 ) dev_info(dev->device,"Release: Removed %i rx buffers held by user.\n", cnt);

   // Find tx buffers still owned by descriptor 
   cnt = 0;
   for (x=dev->txBuffers.baseIdx; x < (dev->txBuffers.baseIdx + dev->txBuffers.count); x++) {
      buff = dmaGetBufferList(&(dev->txBuffers),x);

      if ( buff->userHas == desc ) {
         buff->userHas = NULL;
         dmaQueuePush(&(dev->tq),buff);
         cnt++;
      }
   }

   if ( cnt > 0 ) dev_info(dev->device,"Release: Removed %i tx buffers held by user.\n", cnt);

   // CLear tx queue
   dmaQueueFree(&(desc->q));
   kfree(desc);
   return 0;
}


// Dma_Read
// Called when the device is read from
// Returns read count on success. Error code on failure.
ssize_t Dma_Read(struct file *filp, char *buffer, size_t count, loff_t *f_pos) {
   struct DmaBuffer ** buff;
   struct DmaReadData * rd;
   void *             dp;
   ssize_t            ret;
   size_t             rCnt;
   ssize_t            bCnt;
   ssize_t            x;
   struct DmaDesc   * desc;
   struct DmaDevice * dev;

   desc = (struct DmaDesc *)filp->private_data;
   dev  = desc->dev;

   // Verify that size of passed structure
   if ( (count % sizeof(struct DmaReadData)) != 0 ) {
      dev_warn(dev->device,"Read: Called with incorrect size. Got=%li, Exp=%li\n",
            count,sizeof(struct DmaReadData));
      return(-1);
   }
   rCnt = count / sizeof(struct DmaReadData);
   rd = (struct DmaReadData *)kmalloc(rCnt * sizeof(struct DmaReadData),GFP_KERNEL);
   buff = (struct DmaBuffer **)kmalloc(rCnt * sizeof(struct DmaBuffer *),GFP_KERNEL);

   // Copy read structure
   if ( (ret=copy_from_user(rd,buffer,rCnt * sizeof(struct DmaReadData)))) {
      dev_warn(dev->device,"Read: failed to copy struct from user space ret=%li, user=%p kern=%p\n",
          ret, (void *)buffer, (void *)rd);
      return -1;
   }

   // Get buffers
   bCnt = dmaQueuePopList(&(desc->q),buff,rCnt);

   for (x = 0; x < bCnt; x++ ) {

      // Report frame error
      if ( buff[x]->error )
         dev_warn(dev->device,"Read: error encountered 0x%x.\n", buff[x]->error);

      // Copy associated data
      rd[x].dest   = buff[x]->dest;
      rd[x].flags  = buff[x]->flags;
      rd[x].index  = buff[x]->index;
      rd[x].error  = buff[x]->error;
      rd[x].ret    = buff[x]->size;

      // Convert pointer
      if ( sizeof(void *) == 4 || rd[x].is32 ) dp = (void *)(rd[x].data & 0xFFFFFFFF);
      else dp = (void *)rd[x].data;

      // if pointer is zero, index is used
      if ( dp == 0 ) buff[x]->userHas = desc;

      // Copy data if pointer is provided
      else {

         // User buffer is short
         if ( rd[x].size < buff[x]->size ) {
            dev_warn(dev->device,"Read: user buffer is too small. Rx=%i, User=%i.\n",
               buff[x]->size, (int32_t)rd[x].size);
            rd[x].error |= DMA_ERR_MAX;
            rd[x].ret = -1;
         }

         // Copy to user
         else if ( (ret=copy_to_user(dp, buff[x]->buffAddr, buff[x]->size) )) {
            dev_warn(dev->device,"Read: failed to copy data to user space ret=%li, user=%p kern=%p size=%u.\n",
                ret, dp, buff[x]->buffAddr, buff[x]->size);
            rd[x].ret = -1;
         }

         // Return entry to RX queue
         dev->hwFunc->retRxBuffer(dev,&(buff[x]),1);
      }

      // Debug if enabled
      if ( dev->debug > 0 ) {
         dev_info(dev->device,"Read: Ret=%i, Dest=%i, Flags=0x%.8x, Error=%i.\n",
            rd[x].ret, rd[x].dest, rd[x].flags, rd[x].error);
      }
   }
   kfree(buff);

   if ( (ret=copy_to_user(buffer,rd,rCnt * sizeof(struct DmaReadData)))) {
      dev_warn(dev->device,"Read: failed to copy struct to user space ret=%li, user=%p kern=%p\n",
          ret, (void *)buffer, (void *)&rd);
      x = -1;
   }
   kfree(rd);
   return(bCnt);
}

// Dma_Write
// Called when the device is written to
// Returns write count on success. Error code on failure.
ssize_t Dma_Write(struct file *filp, const char* buffer, size_t count, loff_t* f_pos) {
   ssize_t             ret;
   ssize_t             res;
   void *              dp;
   struct DmaWriteData wr;
   struct DmaBuffer *  buff;
   struct DmaDesc   *  desc;
   struct DmaDevice *  dev;
   uint32_t            destByte;
   uint32_t            destBit;

   desc = (struct DmaDesc *)filp->private_data;
   dev  = desc->dev;

   // Verify that size of passed structure
   if ( count != sizeof(struct DmaWriteData) ) {
      dev_warn(dev->device,"Write: Called with incorrect size. Got=%li, Exp=%li.\n",
            count,sizeof(struct DmaWriteData));
      return(-1);
   }

   // Copy data structure
   if ( (ret=copy_from_user(&wr,buffer,sizeof(struct DmaWriteData))) ) {
      dev_warn(dev->device,"Write: failed to copy struct from user space ret=%li, user=%p kern=%p.\n",
          ret, (void *)buffer, (void *)&wr);
      return(-1);
   }

   // Bad size
   if ( wr.size > dev->cfgSize ) {
      dev_warn(dev->device,"Write: passed size is too large for TX buffer.\n");
      return(-1);
   }

   // Bad destination
   destByte = wr.dest / 8;
   destBit  = 1 << (wr.dest % 8);
   if ( (wr.dest > DMA_MAX_DEST) || ((destBit & dev->destMask[destByte]) == 0 ) ) {
      dev_warn(dev->device,"Write: Invalid destination. Byte %i, Got=0x%x. Mask=0x%x.\n", 
            destByte,destBit,dev->destMask[destByte]);
      return(-1);
   }

   // Convert pointer
   if ( sizeof(void *) == 4 || wr.is32 ) dp = (void *)(wr.data & 0xFFFFFFFF);
   else dp = (void *)wr.data;

   // if pointer is zero, index is used
   if ( dp == 0 ) {

      // First look in tx buffer list then look in rx list 
      // Rx list is alid if user is passing index of previously received buffer
      //if ( ((buff=dmaGetBuffer(dev,wr.index)) == NULL ) || buff->userHas != desc ) {
      if ( (buff=dmaGetBuffer(dev,wr.index)) == NULL ) {
         dev_warn(dev->device,"Write: Invalid index posted: %i.\n", wr.index);
         return(-1);
      }
      buff->userHas = NULL;
   }      

   // Copy data if pointer is provided
   else {

      // Read transmit buffer queue, return 0 if error
      if ((buff = dmaQueuePop(&(dev->tq))) == NULL ) return (0);

      // Copy data from user space.
      if ( (ret = copy_from_user(buff->buffAddr,dp,wr.size)) ) {
         dev_warn(dev->device,"Write: failed to copy data from user space ret=%li, user=%p kern=%p size=%i.\n",
             ret, dp, buff->buffAddr, wr.size);
         dmaQueuePush(&(dev->tq),buff);
         return(-1);
      }
   }

   // Copy remaining fields
   buff->count++;
   buff->dest   = wr.dest;
   buff->flags  = wr.flags;
   buff->size   = wr.size;

   // board specific call 
   res = dev->hwFunc->sendBuffer(dev,&buff,1);

   // Debug
   if ( dev->debug > 0 ) {
      dev_info(dev->device,"Write: Size=%i, Dest=%i, Flags=0x%.8x, res=%li\n",
          buff->size, buff->dest, buff->flags, res);
   }
   if ( res < 0) return(res);
   else return buff->size;
}


// Perform commands
ssize_t Dma_Ioctl(struct file *filp, uint32_t cmd, unsigned long arg) {
   uint8_t newMask[DMA_MASK_SIZE];
   struct DmaDesc   * desc;
   struct DmaDevice * dev;
   struct DmaBuffer * buff;
   struct DmaBuffer ** buffList;

   uint32_t   x;
   uint32_t   cnt;
   uint32_t   bCnt;
   uint32_t * indexes;

   desc = (struct DmaDesc *)filp->private_data;
   dev  = desc->dev;

   // Util Command
   if (cmd & DMA_Util_Cmd_Mask) {
      if ( dev->utilCommand != NULL ) return(dev->utilCommand(dev,cmd,arg));
      else return (-1);
   }

   // Determine command
   switch (cmd & 0xFFFF) {

      // Get buffer count
      case DMA_Get_Buff_Count: 
         return(dev->rxBuffers.count + dev->txBuffers.count);
         break;

      // Get rx buffer count
      case DMA_Get_RxBuff_Count: 
         return(dev->rxBuffers.count);
         break;

      // Get tx buffer count
      case DMA_Get_TxBuff_Count: 
         return(dev->txBuffers.count);
         break;

      // Get buffer size, same size for rx and tx
      case DMA_Get_Buff_Size: 
         return(dev->cfgSize);
         break;

      // Check if read is ready
      case DMA_Read_Ready: 
         return(dmaQueueNotEmpty(&(desc->q)));
         break;

      // Set debug level
      case DMA_Set_Debug:


         dev->debug = arg;
         dev_info(dev->device,"debug set to %u.\n",(uint32_t)arg);
         return(0);
         break;

      // Attempt to reserve destination
      case DMA_Set_Mask:
         memset(newMask,0,DMA_MASK_SIZE);
         ((uint32_t *)newMask)[0] = arg;
         return(Dma_SetMaskBytes(dev,desc,newMask));
         break;

      // Attempt to reserve destination
      case DMA_Set_MaskBytes:
         if ( copy_from_user(newMask,(void *)arg,DMA_MASK_SIZE) ) return(-1);
         return(Dma_SetMaskBytes(dev,desc,newMask));
         break;

      // Return buffer index
      case DMA_Ret_Index:
         cnt = (cmd >> 16) & 0xFFFF;

         if ( cnt == 0 ) return(0);
         indexes = kmalloc(cnt * sizeof(uint32_t),GFP_KERNEL);
         if (copy_from_user(indexes,(void *)arg,(cnt * sizeof(uint32_t)))) return(-1);

         buffList = (struct DmaBuffer **)kmalloc(cnt * sizeof(struct DmaBuffer *),GFP_KERNEL);
         bCnt = 0;

         for (x=0; x < cnt; x++) {

            // Attempt to find buffer in RX list
            if ( (buff = dmaGetBufferList(&(dev->rxBuffers),indexes[x])) != NULL ) {

               // Only return if owned by current desc
               if ( buff->userHas == desc ) {
                  buff->userHas = NULL;
                  buffList[bCnt++] = buff;
               }
            }

            // Attempt to find in tx list
            else if ( (buff = dmaGetBufferList(&(dev->txBuffers),indexes[x])) != NULL ) {

               // Only return if owned by current desc
               if ( buff->userHas == desc ) {
                  buff->userHas = NULL;

                  // Return entry to TX queue
                  dmaQueuePush(&(dev->tq),buff);
               }
            }
            else {
               dev_warn(dev->device,"Command: Invalid index posted: %i.\n", indexes[x]);
               kfree(indexes);
               return(-1);
            }
         }
       
         // Return receive buffers 
         dev->hwFunc->retRxBuffer(dev,buffList,bCnt);

         kfree(buffList);
         kfree(indexes);
         return(0);
         break;

      // Request a write buffer index
      case DMA_Get_Index:

         // Read transmit buffer queue
         buff = dmaQueuePop(&(dev->tq));

         // No buffers are available
         if ( buff == NULL ) return(-1);
         else {
            buff->userHas = desc;

            if ( dev->debug > 0 ) 
               dev_info(dev->device,"Command: Returning buffer %i to user\n",buff->index);
            return(buff->index);
         }
         break;

      // Get API Version
      case DMA_Get_Version:
         return(DMA_VERSION);
         break;

      // Register write
      case DMA_Write_Register:
         return(Dma_WriteRegister(dev,arg));
         break;

      // Register read
      case DMA_Read_Register:
         return(Dma_ReadRegister(dev,arg));
         break;

      // All other commands handled by card specific functions   
      default:
         return(dev->hwFunc->command(dev,cmd,arg));
         break;
   }
   return(0);
}


// Poll/Select
uint32_t Dma_Poll(struct file *filp, poll_table *wait ) {
   struct DmaDesc   * desc;
   struct DmaDevice * dev;

   __u32 mask = 0;

   desc = (struct DmaDesc *)filp->private_data;
   dev  = desc->dev;

   dmaQueuePoll(&(dev->tq),filp,wait);
   dmaQueuePoll(&(desc->q),filp,wait);

   if ( dmaQueueNotEmpty(&(desc->q)) ) mask |= POLLIN  | POLLRDNORM; // Readable
   if ( dmaQueueNotEmpty(&(dev->tq)) ) mask |= POLLOUT | POLLWRNORM; // Writable

   return(mask);
}


// Memory map. Map DMA buffers to user space to eliminate a copy if user chooses
int Dma_Mmap(struct file *filp, struct vm_area_struct *vma) {
   struct DmaDesc   * desc;
   struct DmaDevice * dev;
   struct DmaBuffer * buff;
   phys_addr_t physical;

   off_t    offset;
   off_t    vsize;
   off_t    base;
   off_t    relMap;
   uint32_t idx;
   uint32_t ret;

   desc = (struct DmaDesc *)filp->private_data;
   dev  = desc->dev;

   // Figure out offset and size
   offset = vma->vm_pgoff << PAGE_SHIFT;
   vsize  = vma->vm_end - vma->vm_start;

   // Compute index, rx and tx buffers are the same size
   idx = (uint32_t)(offset / (off_t)dev->cfgSize);

   // Check if index is in the range of buffers
   if ( idx < (dev->rxBuffers.count + dev->txBuffers.count) ) {

      // After we use the offset to figure out the index, we must zero it out so
      // the map call will map to the start of our space from dma_alloc_coherent()
      vma->vm_pgoff = 0;

      // Attempt to find buffer
      if ( (buff = dmaGetBuffer(dev,idx)) == NULL ) {
         dev_warn(dev->device,"map: Invalid index posted: %i.\n", idx);
         return(-1);
      }

      // Size must match the buffer size and offset must be size aligned
      if ( (vsize < dev->cfgSize) || (offset % dev->cfgSize) != 0 ) {
         dev_warn(dev->device,"map: Invalid map size (%li) and offset (%li). cfgSize=%i\n",
               vsize,offset,dev->cfgSize);
         return(-1);
      }

      // Coherent buffer
      if ( dev->cfgMode & BUFF_COHERENT ) {

         // Avoid mapping warnings for x86
#if defined(dma_mmap_coherent) && (! defined(CONFIG_X86))
         ret = dma_mmap_coherent(dev->device,vma,buff->buffAddr,buff->buffHandle,dev->cfgSize);
#else
         ret = remap_pfn_range(vma, vma->vm_start, 
                               virt_to_phys((void *)buff->buffAddr) >> PAGE_SHIFT,
                               vsize,
                               vma->vm_page_prot);
#endif

      }

      // Streaming buffer type or ARM ACP
      else if ( (dev->cfgMode & BUFF_STREAM) || (dev->cfgMode & BUFF_ARM_ACP) ) {
         ret = remap_pfn_range(vma, vma->vm_start, 
                               virt_to_phys((void *)buff->buffAddr) >> PAGE_SHIFT,
                               vsize,
                               vma->vm_page_prot);
      }
      else ret = -1;

      if ( ret < 0 )
         dev_warn(dev->device,"map: Failed to map. start 0x%.8lx, end 0x%.8lx, offset %li, size %li, index %i, Ret=%i.\n",
               vma->vm_start,vma->vm_end,offset,vsize,idx,ret);

      return (ret);
   }

   // Map register space
   else {

      // Compute relative base
      base = (off_t)dev->cfgSize * (dev->rxBuffers.count + dev->txBuffers.count);
      relMap = offset - base;
      physical = dev->baseAddr + relMap;

      // Don't allow mapping below allowed base address
      if ( (dev->base + relMap) < dev->rwBase ) {
         dev_warn(dev->device,"map: Bad map range. start 0x%.8lx, end 0x%.8lx, offset %li, size %li, relMap %li\n",
               vma->vm_start,vma->vm_end,offset,vsize,relMap);
         return -1;
      }

      dev_warn(dev->device,"map: Mapping offset relMap (0x%lx), physical (0x%llx) with size (%li)\n",relMap,physical,vsize);

      ret = io_remap_pfn_range(vma, vma->vm_start, physical >> PAGE_SHIFT, vsize, vma->vm_page_prot);

      if ( ret < 0 ) {
         dev_warn(dev->device,"map: Failed to map. start 0x%.8lx, end 0x%.8lx, offset %li, size %li, relMap %li\n",
               vma->vm_start,vma->vm_end,offset,vsize,relMap);
         return -1;
      }
      return 0;
   }
}

// Flush queue
int Dma_Fasync(int fd, struct file *filp, int mode) {
   struct DmaDesc   * desc;

   desc = (struct DmaDesc *)filp->private_data;
   return fasync_helper(fd, filp, mode, &(desc->async_queue));
}


// Open proc file
int Dma_ProcOpen(struct inode *inode, struct file *file) {
   struct seq_file *sf;
   struct DmaDevice *dev;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
   dev = (struct DmaDevice *)PDE_DATA(inode);
#else
   dev = (struct DmaDevice *)PDE(inode)->data;
#endif

   if ( seq_open(file, &DmaSeqOps) == 0 ) {
      sf = file->private_data;
      sf->private = dev;
      return(0);
   }
   else return(-1);
}


// Sequence start, return 1 on first iteration
void * Dma_SeqStart(struct seq_file *s, loff_t *pos) {
   if ( *pos ==0 ) return ((void *)1);
   else return NULL;
}


// Sequence next, always return NULL
void * Dma_SeqNext(struct seq_file *s, void *v, loff_t *pos) {
   return NULL;
}


// Sequence end
void Dma_SeqStop(struct seq_file *s, void *v) {
   // Nothing to do
}


// Sequence show
int Dma_SeqShow(struct seq_file *s, void *v) {
   struct   DmaBuffer * buff;
   struct   DmaDevice * dev;
   uint32_t max;
   uint32_t min;
   uint32_t sum;
   uint32_t avg;
   uint32_t miss;
   uint32_t userCnt;
   uint32_t hwCnt;
   uint32_t hwQCnt;
   uint32_t qCnt;
   uint32_t x;

   dev = (struct DmaDevice *)s->private;

   // Call applications specific show function first
   dev->hwFunc->seqShow(s,dev);

   seq_printf(s,"\n");
   seq_printf(s,"-------------- General --------------------\n");
   seq_printf(s,"          Dma Version : 0x%x\n",DMA_VERSION);
   seq_printf(s,"          Git Version : " GITV "\n\n");
   seq_printf(s,"-------------- Read Buffers ---------------\n");
   seq_printf(s,"         Buffer Count : %u\n",dev->rxBuffers.count);
   seq_printf(s,"          Buffer Size : %u\n",dev->cfgSize);
   seq_printf(s,"          Buffer Mode : %u\n",dev->cfgMode);

   userCnt = 0;
   hwCnt   = 0;
   hwQCnt   = 0;
   qCnt    = 0;
   miss    = 0;
   max     = 0;
   min     = 0xFFFFFFFF;
   sum     = 0;

   for (x=dev->rxBuffers.baseIdx; x < (dev->rxBuffers.baseIdx + dev->rxBuffers.count); x++) {
      buff = dmaGetBufferList(&(dev->rxBuffers),x);

      if ( buff->count > max ) max = buff->count;
      if ( buff->count < min ) min = buff->count;
      if ( buff->userHas ) userCnt++;
      if (  buff->inHw   && (!buff->inQ) ) hwCnt++;
      if (  buff->inHw   &&  buff->inQ   ) hwQCnt++;
      if ( (!buff->inHw) &&  buff->inQ   ) qCnt++;

      if ( buff->userHas == NULL && buff->inHw == 0 && buff->inQ == 0 ) miss++;

      sum += buff->count;
   }
   if (dev->rxBuffers.count == 0) {
      min = 0;
      avg = 0;
   }
   else avg = sum/dev->rxBuffers.count;

   seq_printf(s,"      Buffers In User : %u\n",userCnt);
   seq_printf(s,"        Buffers In Hw : %u\n",hwCnt);
   seq_printf(s,"  Buffers In Pre-Hw Q : %u\n",hwQCnt);
   seq_printf(s,"  Buffers In Rx Queue : %u\n",qCnt);
   seq_printf(s,"      Missing Buffers : %u\n",miss);
   seq_printf(s,"       Min Buffer Use : %u\n",min);
   seq_printf(s,"       Max Buffer Use : %u\n",max);
   seq_printf(s,"       Avg Buffer Use : %u\n",avg);
   seq_printf(s,"       Tot Buffer Use : %u\n",sum);

   seq_printf(s,"\n");
   seq_printf(s,"-------------- Write Buffers ---------------\n");
   seq_printf(s,"         Buffer Count : %u\n",dev->txBuffers.count);
   seq_printf(s,"          Buffer Size : %u\n",dev->cfgSize);
   seq_printf(s,"          Buffer Mode : %u\n",dev->cfgMode);

   userCnt = 0;
   hwCnt   = 0;
   hwQCnt  = 0;
   qCnt    = 0;
   miss    = 0;
   max     = 0;
   min     = 0xFFFFFFFF;
   sum     = 0;

   for (x=dev->txBuffers.baseIdx; x < (dev->txBuffers.baseIdx + dev->txBuffers.count); x++) {
      buff = dmaGetBufferList(&(dev->txBuffers),x);

      if ( buff->count > max ) max = buff->count;
      if ( buff->count < min ) min = buff->count;
      if ( buff->userHas ) userCnt++;
      if (  buff->inHw   && (!buff->inQ) ) hwCnt++;
      if (  buff->inHw   &&  buff->inQ   ) hwQCnt++;
      if ( (!buff->inHw) &&  buff->inQ   ) qCnt++;

      if ( buff->userHas == NULL && buff->inHw == 0 && buff->inQ == 0 ) miss++;

      sum += buff->count;
   }
   if (dev->txBuffers.count == 0) {
      min = 0;
      avg = 0;
   }
   else avg = sum/dev->txBuffers.count;

   seq_printf(s,"      Buffers In User : %u\n",userCnt);
   seq_printf(s,"        Buffers In Hw : %u\n",hwCnt);
   seq_printf(s,"  Buffers In Pre-Hw Q : %u\n",hwQCnt);
   seq_printf(s,"  Buffers In Sw Queue : %u\n",qCnt);
   seq_printf(s,"      Missing Buffers : %u\n",miss);
   seq_printf(s,"       Min Buffer Use : %u\n",min);
   seq_printf(s,"       Max Buffer Use : %u\n",max);
   seq_printf(s,"       Avg Buffer Use : %u\n",avg);
   seq_printf(s,"       Tot Buffer Use : %u\n",sum);
   seq_printf(s,"\n");

   return 0;
}

// Set Mask
int Dma_SetMaskBytes(struct DmaDevice *dev, struct DmaDesc *desc, uint8_t * mask ) {
   unsigned long iflags;
   uint32_t idx;
   uint32_t destByte;
   uint32_t destBit;

   // Can only be called once
   static const uint8_t zero[DMA_MASK_SIZE] = { 0 };
   if (memcmp(desc->destMask,zero,DMA_MASK_SIZE)) return(-1); 

   // Make sure we can't receive data while adjusting mask flags
   // Interrupts are disabled
   spin_lock_irqsave(&dev->maskLock,iflags);

   // First check if all lockable
   for ( idx=0; idx < DMA_MAX_DEST; idx ++ ) {
      destByte = idx / 8;
      destBit  = 1 << (idx % 8);

      // We want to get this one
      if ( (mask[destByte] & destBit) != 0 ) {
         if ( dev->desc[idx] != NULL ) {
            spin_unlock_irqrestore(&dev->maskLock,iflags);
            if (dev->debug > 0) dev_info(dev->device,"Dma_SetMask: Dest %i already mapped\n",idx);
            return(-1);
         }
      }
   }

   // Next lock the ones we want
   for ( idx=0; idx < DMA_MAX_DEST; idx ++ ) {
      destByte = idx / 8;
      destBit  = 1 << (idx % 8);

      // We want to get this one
      if ( (mask[destByte] & destBit) != 0 ) {
         dev->desc[idx] = desc;
         if (dev->debug > 0) dev_info(dev->device,"Dma_SetMask: Register dest for %i.\n", idx);
      }
   }
   memcpy(desc->destMask,mask,DMA_MASK_SIZE);

   spin_unlock_irqrestore(&dev->maskLock,iflags);
   return(0);
}

// Write Register
int32_t Dma_WriteRegister(struct DmaDevice *dev, uint64_t arg) {
   int32_t  ret;

   struct DmaRegisterData rData;

   if ((ret = copy_from_user(&rData,(void *)arg,sizeof(struct DmaRegisterData)))) {
      dev_warn(dev->device,"Dma_WriteRegister: copy_from_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &rData);
      return(-1);
   }

   if ( ( (dev->base + rData.address) < dev->rwBase ) ||
        ( (dev->base + rData.address + 4) > (dev->rwBase + dev->rwSize) ) ) return(-1);

   iowrite32(rData.data,dev->base+rData.address);

   return(0);
}

// Read Register
int32_t Dma_ReadRegister(struct DmaDevice *dev, uint64_t arg) {
   int32_t  ret;

   struct DmaRegisterData rData;

   if ((ret=copy_from_user(&rData,(void *)arg,sizeof(struct DmaRegisterData)))) {
      dev_warn(dev->device,"Dma_ReadRegister: copy_from_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &rData);
      return(-1);
   }

   if ( ( (dev->base + rData.address) < dev->rwBase ) ||
        ( (dev->base + rData.address + 4) > (dev->rwBase + dev->rwSize) ) ) return(-1);

   rData.data = ioread32(dev->base+rData.address);

   // Return the data structure
   if ((ret=copy_to_user((void *)arg,&rData,sizeof(struct DmaRegisterData)))) {
      dev_warn(dev->device,"Dma_ReadRegister: copy_to_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &rData);
      return(-1);
   }
   return(0);
}

