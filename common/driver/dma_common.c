/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    This file implements the common functionalities required for Direct
 *    Memory Access (DMA) operations within the kernel space. It provides
 *    a set of utility functions and structures to facilitate DMA transfers
 *    between the CPU and peripheral devices, aiming to abstract and simplify
 *    the DMA API usage for different hardware configurations.
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

#ifndef RHEL_RELEASE_VERSION
#define RHEL_RELEASE_VERSION(...) 0
#endif

/**
 * struct DmaFunctions - Define interface routines for DMA operations
 * @owner:          Pointer to the module owner of this structure
 * @read:           Pointer to the function that handles read operations
 * @write:          Pointer to the function that handles write operations
 * @open:           Pointer to the function that handles open operations
 * @release:        Pointer to the function that handles release operations
 * @poll:           Pointer to the function that handles poll operations
 * @fasync:         Pointer to the function that handles fasync operations
 * @unlocked_ioctl: Pointer to the function that handles ioctl operations without the BKL
 * @compat_ioctl:   Pointer to the function that handles ioctl operations for compatibility
 * @mmap:           Pointer to the function that handles memory mapping operations
 *
 * This structure defines the file operations for DMA (Direct Memory Access)
 * interface routines. Each field represents a specific operation in the file
 * operations interface, allowing the kernel to interact with DMA devices.
 */
struct file_operations DmaFunctions = {
   .owner          = THIS_MODULE,
   .read           = Dma_Read,
   .write          = Dma_Write,
   .open           = Dma_Open,
   .release        = Dma_Release,
   .poll           = Dma_Poll,
   .fasync         = Dma_Fasync,
   .unlocked_ioctl = (void *)Dma_Ioctl,
   .compat_ioctl   = (void *)Dma_Ioctl,
   .mmap           = Dma_Mmap,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)

/**
 * struct DmaProcOps - proc file operations for DMA
 *
 * Setup proc file operations for Direct Memory Access (DMA) management
 * via procfs. This structure is used for kernel versions 5.6.0 and above.
 *
 * @proc_open:    Pointer to the function to open the proc file.
 * @proc_read:    Pointer to the function to read from the proc file.
 * @proc_lseek:   Pointer to the function to seek within the proc file.
 * @proc_release: Pointer to the function to release the proc file.
 */
static struct proc_ops DmaProcOps = {
   .proc_open    = Dma_ProcOpen,
   .proc_read    = seq_read,
   .proc_lseek   = seq_lseek,
   .proc_release = seq_release
};

#else

/**
 * struct DmaProcOps - file operations for DMA
 *
 * Setup file operations for Direct Memory Access (DMA) management
 * via procfs. This structure is used for kernel versions below 5.6.0.
 *
 * @owner:    Module owner.
 * @open:     Pointer to the function to open the proc file.
 * @read:     Pointer to the function to read from the proc file.
 * @llseek:   Pointer to the function to seek within the proc file.
 * @release:  Pointer to the function to release the proc file.
 */
static struct file_operations DmaProcOps = {
   .owner   = THIS_MODULE,
   .open    = Dma_ProcOpen,
   .read    = seq_read,
   .llseek  = seq_lseek,
   .release = seq_release
};

#endif

/**
 * struct DmaSeqOps - Sequence operations for DMA.
 *
 * This structure defines the sequence operations for DMA handling.
 * It includes methods to start, stop, get the next element, and
 * display information about the current element in the sequence.
 *
 * @start: Pointer to the function that initiates the sequence.
 * @next: Pointer to the function that moves to the next element in the sequence.
 * @stop: Pointer to the function that stops the sequence.
 * @show: Pointer to the function that displays the current element.
 */
static struct seq_operations DmaSeqOps = {
   .start = Dma_SeqStart,   ///< Start the sequence
   .next  = Dma_SeqNext,    ///< Move to the next element
   .stop  = Dma_SeqStop,    ///< Stop the sequence
   .show  = Dma_SeqShow     ///< Display the current element
};

/**
 * gDmaDevCount - Number of active DMA devices.
 */
uint32_t gDmaDevCount;

/**
 * gCl - Global variable for the DMA device class.
 */
struct class *gCl;

/**
 * Dma_DevNode - Devnode callback to set permissions of created devices.
 * @dev: Pointer to the device structure.
 * @mode: Pointer to the mode to be set.
 *
 * This function is used to set the device file permissions to be
 * accessible by all users (0666).
 *
 * Returns NULL always.
 */
char *Dma_DevNode(struct device *dev, umode_t *mode) {
   if (mode != NULL) {
      *mode = 0666;
   }
   return NULL;
}

/**
 * Dma_UnmapReg - Unmaps and releases memory region allocated to a DMA device.
 * @dev: Pointer to the DmaDevice structure.
 *
 * This function unmaps the device I/O memory and releases the memory
 * region allocated to the DMA device, ensuring that the device's
 * resources are properly cleaned up.
 */
void Dma_UnmapReg(struct DmaDevice *dev) {
   // Release the allocated memory region
   release_mem_region(dev->baseAddr, dev->baseSize);

   // Unmap the device I/O memory
   iounmap(dev->base);
}

/**
 * Dma_MapReg - Maps the address space in the buffer for DMA operations
 * @dev: pointer to the DmaDevice structure
 *
 * This function attempts to map the device's register space into memory,
 * using ioremap or ioremap_nocache depending on the kernel version.
 * It also requests the memory region to prevent other drivers from
 * claiming the same space. If mapping or memory request fails, it
 * logs an error and returns -1. On success, it returns 0.
 *
 * Return: 0 on success, -1 on failure.
 */
int Dma_MapReg(struct DmaDevice *dev) {
   if (dev->base == NULL) {
      dev_info(dev->device, "Init: Mapping Register space 0x%llx with size 0x%x.\n", (uint64_t)dev->baseAddr, dev->baseSize);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
      dev->base = ioremap(dev->baseAddr, dev->baseSize);
#else
      dev->base = ioremap_nocache(dev->baseAddr, dev->baseSize);
#endif

      if (!dev->base) {
         dev_err(dev->device, "Init: Could not remap memory.\n");
         return -1;
      }
      dev->reg = dev->base;
      dev_info(dev->device, "Init: Mapped to 0x%p.\n", dev->base);

      // Hold memory region
      if (request_mem_region(dev->baseAddr, dev->baseSize, dev->devName) == NULL) {
         dev_err(dev->device, "Init: Memory in use.\n");
         iounmap(dev->base);
         return -1;
      }
   }
   return 0;
}

/**
 * Dma_Init - Initialize the DMA device
 * @dev: pointer to the DmaDevice structure
 *
 * This function initializes a DMA device, setting up device numbers,
 * character device, device class, proc entry, buffer allocations, and
 * interrupt request. It is called from the top-level probe function.
 * Returns 0 on success, or a negative error code on failure.
 */
int Dma_Init(struct DmaDevice *dev) {
   uint32_t x;
   ssize_t res;
   uint64_t tot;

   // Note the debug flag set
   if (dev->debug) {
      dev_info(dev->device, "Init: Debug logging enabled\n");
   }

   // Allocate device numbers for character device. 1 minor numer starting at 0
   res = alloc_chrdev_region(&(dev->devNum), 0, 1, dev->devName);
   if (res < 0) {
      dev_err(dev->device, "Init: Cannot register char device\n");
      return -1;
   }

   // Initialize the device
   cdev_init(&(dev->charDev), &DmaFunctions);
   dev->major = MAJOR(dev->devNum);

   // Add the character device
   if (cdev_add(&(dev->charDev), dev->devNum, 1) == -1) {
      dev_err(dev->device, "Init: Failed to add device file.\n");
      goto cleanup_alloc_chrdev_region;
   }

   // Create class struct if it does not already exist
   if (gCl == NULL) {
      dev_info(dev->device, "Init: Creating device class\n");

      // RHEL9.4+ backported this breaking change from kernel 6.4.0
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0) || (defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9, 4))
      gCl = class_create(dev->devName);
#else
      gCl = class_create(THIS_MODULE, dev->devName);
#endif

      if (gCl == NULL) {
         dev_err(dev->device, "Init: Failed to create device class\n");
         goto cleanup_cdev_add;
      }

      gCl->devnode = (void *)Dma_DevNode;
   }

   // Attempt to create the device
   if (device_create(gCl, NULL, dev->devNum, NULL, "%s", dev->devName) == NULL) {
      dev_err(dev->device, "Init: Failed to create device file\n");
      goto cleanup_class_create;
   }


   // Setup /proc
   if (NULL == proc_create_data(dev->devName, 0, NULL, &DmaProcOps, dev)) {
      dev_err(dev->device, "Init: Failed to create proc entry.\n");
      goto cleanup_device_create;
   }

   // Remap the I/O register block for safe access
   if ( Dma_MapReg(dev) < 0 ) {
      dev_err(dev->device, "Init: Failed to map register block.\n");
      goto cleanup_proc_create_data;
   }

   // Initialize descriptors
   for (x=0; x < DMA_MAX_DEST; x++) dev->desc[x] = NULL;

   // Initialize locks
   spin_lock_init(&(dev->writeHwLock));
   spin_lock_init(&(dev->commandLock));
   spin_lock_init(&(dev->maskLock));

   // Create TX buffers
   dev_info(dev->device, "Init: Creating %i TX Buffers. Size=%i Bytes. Mode=%i.\n",
        dev->cfgTxCount, dev->cfgSize, dev->cfgMode);
   res = (ssize_t)dmaAllocBuffers(dev, &(dev->txBuffers), dev->cfgTxCount, 0, DMA_TO_DEVICE);
   tot = (uint64_t)res * dev->cfgSize;

   dev_info(dev->device, "Init: Created  %zi out of %i TX Buffers. %llu Bytes.\n", res, dev->cfgTxCount, tot);

   // Handle bad buffer allocation for TX
   if ( dev->cfgTxCount > 0 && res == 0 )
      goto cleanup_dma_mapreg;

   // Initialize transmit queue
   res = (ssize_t)dmaQueueInit(&(dev->tq), dev->txBuffers.count);
   if (res == 0 && dev->txBuffers.count > 0) {
      dev_err(dev->device, "dmaQueueInit: Failed to initialize DMA queues.\n");
      goto cleanup_tx_buffers;
   }

   // Populate transmit queue
   for (x=dev->txBuffers.baseIdx; x < (dev->txBuffers.baseIdx + dev->txBuffers.count); x++)
      dmaQueuePush(&(dev->tq), dmaGetBufferList(&(dev->txBuffers), x));

   // Create RX buffers, bidirectional because RX buffers can be passed to TX
   dev_info(dev->device, "Init: Creating %i RX Buffers. Size=%i Bytes. Mode=%i.\n",
        dev->cfgRxCount, dev->cfgSize, dev->cfgMode);
   res = (ssize_t)dmaAllocBuffers(dev, &(dev->rxBuffers), dev->cfgRxCount, dev->txBuffers.count, DMA_BIDIRECTIONAL);
   tot = (uint64_t)res * dev->cfgSize;

   dev_info(dev->device, "Init: Created  %zi out of %i RX Buffers. %llu Bytes.\n", res, dev->cfgRxCount, tot);

   // Bad buffer allocation
   if ( dev->cfgRxCount > 0 && res == 0 )
      goto cleanup_dma_queue;

   // Call card specific init
   dev->hwFunc->init(dev);

   // Set interrupt
   if ( dev->irq != 0 ) {
      dev_info(dev->device, "Init: IRQ %d\n", dev->irq);
      res = request_irq(dev->irq, dev->hwFunc->irq, IRQF_SHARED, dev->devName, (void*)dev);

      // Result of request IRQ from OS.
      if (res < 0) {
         dev_err(dev->device, "Init: Unable to allocate IRQ.");
         goto cleanup_card_clear;
      }
   }

   // Enable card
   dev->hwFunc->enable(dev);
   return 0;

   /* Clean mess on failure */

cleanup_card_clear:
   dev->hwFunc->clear(dev);

// Clean RX buffers
   dmaFreeBuffers(&(dev->rxBuffers));

cleanup_dma_queue:
   dmaQueueFree(&(dev->tq));

cleanup_tx_buffers:
   dmaFreeBuffers(&(dev->txBuffers));

cleanup_dma_mapreg:
   Dma_UnmapReg(dev);

cleanup_proc_create_data:
   remove_proc_entry(dev->devName, NULL);

cleanup_device_create:
   if ( gCl != NULL ) device_destroy(gCl, dev->devNum);

cleanup_class_create:
   if (gDmaDevCount == 0 && gCl != NULL) {
      class_destroy(gCl);
      gCl = NULL;
   }

cleanup_cdev_add:
   cdev_del(&(dev->charDev));

cleanup_alloc_chrdev_region:
   unregister_chrdev_region(dev->devNum, 1);

   return -1;
}

/**
 * Dma_Clean - Cleanup device resources.
 * @dev: Pointer to the device structure.
 *
 * This function is called from the top-level remove function. It performs
 * the necessary cleanup for a DMA device. It calls the card-specific clear
 * function, releases the IRQ if allocated, frees both RX and TX buffers,
 * clears the transmission queue, and unmaps device registers. Additionally,
 * it handles the removal of the device from the system and cleans up device
 * driver registration and class destruction if this is the last device.
 */
void Dma_Clean(struct DmaDevice *dev) {
   uint32_t x;

   // Disable interrupts on the card itself
   if (dev->hwFunc)
      dev->hwFunc->irqEnable(dev, 0);

   // Release IRQ if allocated.
   if (dev->irq != 0) {
      free_irq(dev->irq, dev);
   }

   // Call card-specific clear function.
   if (dev->hwFunc)
      dev->hwFunc->clear(dev);

   // Free RX and TX buffers.
   dmaFreeBuffers(&(dev->rxBuffers));
   dmaFreeBuffers(&(dev->txBuffers));

   // Clear the transmission queue.
   dmaQueueFree(&(dev->tq));

   // Clear descriptors if they exist.
   for (x = 0; x < DMA_MAX_DEST; x++) {
      dev->desc[x] = NULL;
   }

   // Unmap device registers.
   Dma_UnmapReg(dev);

   // Remove proc entry and delete character device.
   remove_proc_entry(dev->devName, NULL);
   cdev_del(&(dev->charDev));

   // Unregister device driver. Necessary, but causes kernel crash on removal.
   // Added safety check for 'gCl' to avoid potential null pointer dereference.
   if (gCl != NULL) {
      device_destroy(gCl, dev->devNum);
   } else {
      dev_warn(dev->device, "Clean: gCl is already NULL.\n");
   }

   // Unregister character device region.
   unregister_chrdev_region(dev->devNum, 1);

   // Check if this is the last device and clean up accordingly.
   if (gDmaDevCount == 0 && gCl != NULL) {
      dev_info(dev->device, "Clean: Destroying device class\n");
      class_destroy(gCl);
      gCl = NULL;
   }

   // Zero out the device structure as part of cleanup.
   memset(dev, 0, sizeof(struct DmaDevice));
}

/**
 * Dma_Open - Open function for the DMA device
 * @inode: pointer to the inode structure
 * @filp: file pointer
 *
 * This function is called whenever the device file is opened in userspace.
 * It allocates and initializes a DMA descriptor for the device, setting up
 * the necessary structures for DMA operations. The initialized descriptor
 * is stored in the file's private data for later use by other operations.
 *
 * Return: 0 on success, or an error code on failure.
 */
int Dma_Open(struct inode *inode, struct file *filp) {
   struct DmaDevice *dev;
   struct DmaDesc *desc;

   // Find the device structure from the inode
   dev = container_of(inode->i_cdev, struct DmaDevice, charDev);

   // Allocate and initialize the DMA descriptor
   desc = (struct DmaDesc *)kzalloc(sizeof(struct DmaDesc), GFP_KERNEL);
   if (!desc) {
      dev_err(dev->device, "Open: kzalloc failed\n");
      return -ENOMEM;  // Return an error if allocation fails
   }

   memset(desc, 0, sizeof(struct DmaDesc));
   dmaQueueInit(&(desc->q), dev->cfgRxCount);
   desc->async_queue = NULL;
   desc->dev = dev;

   // Store the descriptor in the file's private data for later use
   filp->private_data = desc;
   return 0;
}

/**
 * Dma_Release - Release DMA resources
 * @inode: Pointer to the inode structure
 * @filp: File structure pointer
 *
 * This function is called when the device is closed. It releases DMA buffers
 * and cleans up device-specific data structures. It ensures that all tx and rx
 * buffers that are still owned by the descriptor are properly released and that
 * the descriptor itself is freed. Additionally, it handles the detachment of the
 * device from asynchronous notification structures if necessary.
 *
 * Return: Returns 0 on success, error code on failure.
 */
int Dma_Release(struct inode *inode, struct file *filp) {
   struct DmaDesc   *desc;
   struct DmaDevice *dev;
   struct DmaBuffer *buff;

   unsigned long iflags;
   uint32_t x;
   uint32_t cnt;
   uint32_t destByte;
   uint32_t destBit;

   // Obtain device and descriptor from file's private data
   desc = (struct DmaDesc *)filp->private_data;
   dev = desc->dev;

   // Prevent data reception during mask adjustment
   spin_lock_irqsave(&dev->maskLock, iflags);

   // Clear device descriptor pointers based on destMask
   for (x = 0; x < DMA_MAX_DEST; x++) {
      destByte = x / 8;
      destBit = 1 << (x % 8);
      if ((destBit & desc->destMask[destByte]) != 0) {
         dev->desc[x] = NULL;
      }
   }

   // Restore interrupts
   spin_unlock_irqrestore(&dev->maskLock, iflags);

   // Detach from asynchronous notification structures if necessary
   if (desc->async_queue) {
      Dma_Fasync(-1, filp, 0);
   }

   // Release DMA buffers from the descriptor's queue
   cnt = 0;
   while ((buff = dmaQueuePop(&(desc->q))) != NULL) {
      dev->hwFunc->retRxBuffer(dev, &buff, 1);
      cnt++;
   }
   if (cnt > 0) {
      dev_info(dev->device, "Release: Removed %i buffers from closed device.\n", cnt);
   }

   // Release rx buffers still owned by the descriptor
   cnt = 0;
   for (x = dev->rxBuffers.baseIdx; x < (dev->rxBuffers.baseIdx + dev->rxBuffers.count); x++) {
      buff = dmaGetBufferList(&(dev->rxBuffers), x);

      if (buff->userHas == desc) {
         buff->userHas = NULL;
         dev->hwFunc->retRxBuffer(dev, &buff, 1);
         cnt++;
      }
   }
   if (cnt > 0) {
      dev_info(dev->device, "Release: Removed %i rx buffers held by user.\n", cnt);
   }

   // Release tx buffers still owned by the descriptor
   cnt = 0;
   for (x = dev->txBuffers.baseIdx; x < (dev->txBuffers.baseIdx + dev->txBuffers.count); x++) {
      buff = dmaGetBufferList(&(dev->txBuffers), x);

      if (buff->userHas == desc) {
         buff->userHas = NULL;
         dmaQueuePush(&(dev->tq), buff);
         cnt++;
      }
   }
   if (cnt > 0) {
      dev_info(dev->device, "Release: Removed %i tx buffers held by user.\n", cnt);
   }

   // Clear the tx queue and free the descriptor
   dmaQueueFree(&(desc->q));
   kfree(desc);
   return 0;
}

/**
 * Dma_Read - Read data from a DMA buffer
 * @filp: The file pointer associated with the device
 * @buffer: User space buffer to read the data into
 * @count: The size of the data to read
 * @f_pos: The offset within the file
 *
 * This function is called when the device is read from. It reads data from a DMA buffer
 * into a user space buffer. It verifies the size of the passed structure, allocates
 * necessary buffers, copies data from kernel space to user space, and handles errors.
 *
 * Return: The number of read structures on success or an error code on failure.
 */
ssize_t Dma_Read(struct file *filp, __user char *buffer, size_t count, loff_t *f_pos) {
   struct DmaBuffer **buff;
   struct DmaReadData *rd;
   __user void *dp;
   uint64_t ret;
   size_t rCnt;
   ssize_t bCnt;
   ssize_t x;
   struct DmaDesc *desc;
   struct DmaDevice *dev;

   desc = (struct DmaDesc *)filp->private_data;
   dev = desc->dev;

   // Verify the size of the passed structure
   if ((count % sizeof(struct DmaReadData)) != 0) {
      dev_warn(dev->device, "Read: Called with incorrect size. Got=%li, Exp=%li\n",
               count, sizeof(struct DmaReadData));
      return -1;
   }

   rCnt = count / sizeof(struct DmaReadData);
   rd = (struct DmaReadData *)kzalloc(rCnt * sizeof(struct DmaReadData), GFP_KERNEL);
   if (!rd) {
      dev_warn(dev->device, "Read: Failed to allocate DmaReadData block of %ld bytes\n",
         (ulong)(rCnt * sizeof(struct DmaReadData)));
      return -ENOMEM;
   }

   buff = (struct DmaBuffer **)kzalloc(rCnt * sizeof(struct DmaBuffer *), GFP_KERNEL);
   if (!buff) {
      dev_warn(dev->device, "Read: Failed to allocate DmaBuffer descriptor block of %ld bytes\n",
         (ulong)(rCnt * sizeof(struct DmaBuffer*)));
      kfree(rd);
      return -ENOMEM;
   }
   // Copy the read structure from user space
   if ((ret = copy_from_user(rd, buffer, rCnt * sizeof(struct DmaReadData)))) {
      dev_warn(dev->device, "Read: failed to copy struct from user space ret=%llu, user=%p kern=%p\n",
               ret, buffer, rd);
      return -1;
   }

   // Get buffers from the DMA queue
   bCnt = dmaQueuePopList(&(desc->q), buff, rCnt);

   for (x = 0; x < bCnt; x++) {
      // Report frame error
      if (buff[x]->error)
         dev_warn(dev->device, "Read: error encountered 0x%x.\n", buff[x]->error);

      // Copy associated data to the read structure
      rd[x].dest = buff[x]->dest;
      rd[x].flags = buff[x]->flags;
      rd[x].index = buff[x]->index;
      rd[x].error = buff[x]->error;
      rd[x].ret = (int32_t)buff[x]->size;

      // Convert pointer based on architecture
      if (sizeof(void *) == 4 || rd[x].is32)
         dp = (__user void *)(rd[x].data & 0xFFFFFFFF);
      else
         dp = (__user void *)rd[x].data;

      // Use index if pointer is zero
      if (dp == NULL) {
          buff[x]->userHas = desc;
      } else {
         // Warn if user buffer is too small
         if (rd[x].size < buff[x]->size) {
            dev_warn(dev->device, "Read: user buffer is too small. Rx=%i, User=%i.\n",
                     buff[x]->size, (int32_t)rd[x].size);
            rd[x].error |= DMA_ERR_MAX;
            rd[x].ret = -1;

         // Copy data to user space
         } else if ((ret = copy_to_user(dp, buff[x]->buffAddr, buff[x]->size))) {
            dev_warn(dev->device, "Read: failed to copy data to user space ret=%llu, user=%p kern=%p size=%u.\n",
                     ret, dp, buff[x]->buffAddr, buff[x]->size);
            rd[x].ret = -1;
         }

         // Return entry to RX queue
         dev->hwFunc->retRxBuffer(dev, &(buff[x]), 1);
      }

      // Debug information
      if (dev->debug > 0) {
         dev_info(dev->device, "Read: Ret=%i, Dest=%i, Flags=0x%.8x, Error=%i.\n",
                  rd[x].ret, rd[x].dest, rd[x].flags, rd[x].error);
      }
   }
   kfree(buff);

   // Copy the read structure back to user space
   if ((ret = copy_to_user(buffer, rd, rCnt * sizeof(struct DmaReadData)))) {
      dev_warn(dev->device, "Read: failed to copy struct to user space ret=%llu, user=%p kern=%p\n",
               ret, buffer, &rd);
   }
   kfree(rd);
   return bCnt;
}

/**
 * Dma_Write - Handle write operations for a DMA device
 * @filp: pointer to the file structure
 * @buffer: user space buffer to write data from
 * @count: size of the data to write
 * @f_pos: offset in the file (ignored in this context)
 *
 * This function is called when a write operation is performed on the DMA device.
 * It performs various checks and operations to ensure the write is valid, including
 * verifying the size of the data, copying the data from user space, and handling
 * buffer management for DMA transactions.
 *
 * Return: Number of bytes written on success, negative error code on failure.
 */
ssize_t Dma_Write(struct file *filp, __user const char *buffer, size_t count, loff_t *f_pos) {
   uint64_t ret;
   ssize_t res;
   __user void *dp;
   struct DmaWriteData wr;
   struct DmaBuffer *buff;
   struct DmaDesc *desc;
   struct DmaDevice *dev;
   uint32_t destByte;
   uint32_t destBit;

   desc = (struct DmaDesc *)filp->private_data;
   dev = desc->dev;

   // Verify the size of the passed structure
   if (count != sizeof(struct DmaWriteData)) {
      dev_warn(dev->device, "Write: Called with incorrect size. Got=%li, Exp=%li.\n",
               count, sizeof(struct DmaWriteData));
      return -1;
   }

   // Copy data structure from user space
   if ((ret = copy_from_user(&wr, buffer, sizeof(struct DmaWriteData)))) {
      dev_warn(dev->device, "Write: failed to copy struct from user space ret=%llu, user=%p kern=%p.\n",
               ret, buffer, &wr);
      return -1;
   }

   // Validate passed size against configuration
   if (wr.size > dev->cfgSize) {
      dev_warn(dev->device, "Write: passed size is too large for TX buffer.\n");
      return -1;
   }

   // Validate destination
   destByte = wr.dest / 8;
   destBit = 1 << (wr.dest % 8);
   if ((wr.dest > DMA_MAX_DEST) || ((destBit & dev->destMask[destByte]) == 0)) {
      dev_warn(dev->device, "Write: Invalid destination. Byte %i, Got=0x%x. Mask=0x%x.\n",
               destByte, destBit, dev->destMask[destByte]);
      return -1;
   }

   // Convert pointer based on architecture or request
   if (sizeof(void *) == 4 || wr.is32) {
       dp = (__user void *)(wr.data & 0xFFFFFFFF);
   } else {
       dp = (__user void *)wr.data;
   }

   // Use index if pointer is null
   if (dp == NULL) {
      if ((buff = dmaGetBuffer(dev, wr.index)) == NULL) {
         dev_warn(dev->device, "Write: Invalid index posted: %i.\n", wr.index);
         return -1;
      }
      buff->userHas = NULL;
   } else {
      // Retrieve a transmit buffer and copy data from user space
      if ((buff = dmaQueuePop(&(dev->tq))) == NULL) return 0;

      if ((ret = copy_from_user(buff->buffAddr, dp, wr.size))) {
         dev_warn(dev->device, "Write: failed to copy data from user space ret=%llu, user=%p kern=%p size=%i.\n",
                  ret, dp, buff->buffAddr, wr.size);
         dmaQueuePush(&(dev->tq), buff);
         return -1;
      }
   }

   // Update buffer fields
   buff->count++;
   buff->dest = wr.dest;
   buff->flags = wr.flags;
   buff->size = wr.size;

   // Board-specific buffer handling
   res = dev->hwFunc->sendBuffer(dev, &buff, 1);

   // Log for debugging
   if (dev->debug > 0) {
      dev_info(dev->device, "Write: Size=%i, Dest=%i, Flags=0x%.8x, res=%li\n",
               buff->size, buff->dest, buff->flags, res);
   }
   if (res < 0) {
       return res;
   } else {
       return buff->size;
   }
}

/**
 * Dma_Ioctl - Perform commands on DMA device
 * @filp: pointer to the file structure
 * @cmd: command to execute
 * @arg: argument for the command
 *
 * This function handles various IOCTL commands for managing DMA operations,
 * including getting buffer counts, setting debug levels, reserving destinations,
 * and reading or writing to registers. It operates directly on the DMA device
 * represented by @filp.
 *
 * Return: Depends on the IOCTL command; usually the result of the command, 0 on success,
 * or a negative error code on failure.
 */
ssize_t Dma_Ioctl(struct file *filp, uint32_t cmd, unsigned long arg) {
   uint8_t newMask[DMA_MASK_SIZE];
   struct DmaDesc   * desc;
   struct DmaDevice * dev;
   struct DmaBuffer * buff;
   struct DmaBuffer ** buffList;

   uint32_t   x;
   uint32_t   cnt;
   uint32_t   bCnt;
   uint32_t   userCnt;
   uint32_t   hwCnt;
   uint32_t   hwQCnt;
   uint32_t   qCnt;
   uint32_t   miss;
   uint32_t * indexes;

   desc = (struct DmaDesc *)filp->private_data;
   dev  = desc->dev;

   // Determine command
   switch (cmd & 0xFFFF) {
      // Get buffer count
      case DMA_Get_Buff_Count:
         return dev->rxBuffers.count + dev->txBuffers.count;
         break;

      // Get rx buffer count
      case DMA_Get_RxBuff_Count:
         return dev->rxBuffers.count;
         break;

      // Get rx buffer in User count
      case DMA_Get_RxBuffinUser_Count:
         userCnt    = 0;
         for (x=dev->rxBuffers.baseIdx; x < (dev->rxBuffers.baseIdx + dev->rxBuffers.count); x++) {
            buff = dmaGetBufferList(&(dev->rxBuffers), x);
            if (  buff->userHas   ) userCnt++;
         }
         return userCnt;
         break;

      // Get rx buffer in HW count
      case DMA_Get_RxBuffinHW_Count:
         hwCnt    = 0;
         for (x=dev->rxBuffers.baseIdx; x < (dev->rxBuffers.baseIdx + dev->rxBuffers.count); x++) {
            buff = dmaGetBufferList(&(dev->rxBuffers), x);
            if ( buff->inHw &&  (!buff->inQ)   ) hwCnt++;
         }
         return hwCnt;
         break;

      // Get rx buffer in Pre-HW Queue count
      case DMA_Get_RxBuffinPreHWQ_Count:
         hwQCnt    = 0;
         for (x=dev->rxBuffers.baseIdx; x < (dev->rxBuffers.baseIdx + dev->rxBuffers.count); x++) {
            buff = dmaGetBufferList(&(dev->rxBuffers), x);
            if ( buff->inHw &&  buff->inQ   ) hwQCnt++;
         }
         return hwQCnt;
         break;

      // Get rx buffer in SW Queue count
      case DMA_Get_RxBuffinSWQ_Count:
         qCnt    = 0;
         for (x=dev->rxBuffers.baseIdx; x < (dev->rxBuffers.baseIdx + dev->rxBuffers.count); x++) {
            buff = dmaGetBufferList(&(dev->rxBuffers), x);
            if ( (!buff->inHw) &&  buff->inQ   ) qCnt++;
         }
         return qCnt;
         break;

      // Get rx buffer missing count
      case DMA_Get_RxBuffMiss_Count:
         miss    = 0;
         for (x=dev->rxBuffers.baseIdx; x < (dev->rxBuffers.baseIdx + dev->rxBuffers.count); x++) {
            buff = dmaGetBufferList(&(dev->rxBuffers), x);
            if ((buff->userHas == NULL) && (buff->inHw == 0) && (buff->inQ == 0)) {
               miss++;
            }
         }
         return miss;
         break;

      // Get tx buffer count
      case DMA_Get_TxBuff_Count:
         return dev->txBuffers.count;
         break;

      // Get tx buffer in User count
      case DMA_Get_TxBuffinUser_Count:
         userCnt    = 0;
         for (x=dev->txBuffers.baseIdx; x < (dev->txBuffers.baseIdx + dev->txBuffers.count); x++) {
            buff = dmaGetBufferList(&(dev->txBuffers), x);
            if (  buff->userHas   ) userCnt++;
         }
         return userCnt;
         break;

      // Get tx buffer in HW count
      case DMA_Get_TxBuffinHW_Count:
         hwCnt    = 0;
         for (x=dev->txBuffers.baseIdx; x < (dev->txBuffers.baseIdx + dev->txBuffers.count); x++) {
            buff = dmaGetBufferList(&(dev->txBuffers), x);
            if ( buff->inHw &&  (!buff->inQ)   ) hwCnt++;
         }
         return hwCnt;
         break;

      // Get tx buffer in Pre-HW Queue count
      case DMA_Get_TxBuffinPreHWQ_Count:
         hwQCnt    = 0;
         for (x=dev->txBuffers.baseIdx; x < (dev->txBuffers.baseIdx + dev->txBuffers.count); x++) {
            buff = dmaGetBufferList(&(dev->txBuffers), x);
            if ( buff->inHw &&  buff->inQ   ) hwQCnt++;
         }
         return hwQCnt;
         break;

      // Get tx buffer in SW Queue count
      case DMA_Get_TxBuffinSWQ_Count:
         qCnt    = 0;
         for (x=dev->txBuffers.baseIdx; x < (dev->txBuffers.baseIdx + dev->txBuffers.count); x++) {
            buff = dmaGetBufferList(&(dev->txBuffers), x);
            if ( (!buff->inHw) &&  buff->inQ   ) qCnt++;
         }
         return qCnt;
         break;

      // Get tx buffer missing count
      case DMA_Get_TxBuffMiss_Count:
         miss    = 0;
         for (x=dev->txBuffers.baseIdx; x < (dev->txBuffers.baseIdx + dev->txBuffers.count); x++) {
            buff = dmaGetBufferList(&(dev->txBuffers), x);
            if ((buff->userHas == NULL) && (buff->inHw == 0) && (buff->inQ == 0)) {
                miss++;
            }
         }
         return miss;
         break;

      // Get buffer size, same size for rx and tx
      case DMA_Get_Buff_Size:
         return dev->cfgSize;
         break;

      // Check if read is ready
      case DMA_Read_Ready:
         return dmaQueueNotEmpty(&(desc->q));
         break;

      // Set debug level
      case DMA_Set_Debug:
         dev->debug = arg;
         dev_info(dev->device, "debug set to %u.\n", (uint32_t)arg);
         return 0;
         break;

      // Attempt to reserve destination
      case DMA_Set_Mask:
         memset(newMask, 0, DMA_MASK_SIZE);
         ((uint32_t *)newMask)[0] = arg;
         return Dma_SetMaskBytes(dev, desc, newMask);
         break;

      // Attempt to reserve destination
      case DMA_Set_MaskBytes:
         if ( copy_from_user(newMask, (__user void *)arg, DMA_MASK_SIZE) ) return -1;
         return Dma_SetMaskBytes(dev, desc, newMask);
         break;

      // Return buffer index
      case DMA_Ret_Index:
         cnt = (cmd >> 16) & 0xFFFF;

         if ( cnt == 0 ) return 0;
         indexes = kzalloc(cnt * sizeof(uint32_t), GFP_KERNEL);
         if (!indexes) {
            dev_warn(dev->device, "Command: Failed to allocate index block of %ld bytes\n",
               (ulong)(cnt * sizeof(uint32_t)));
            return -ENOMEM;
         }
         if (copy_from_user(indexes, (__user void *)arg, (cnt * sizeof(uint32_t)))) return -1;

         buffList = (struct DmaBuffer **)kzalloc(cnt * sizeof(struct DmaBuffer *), GFP_KERNEL);
         if (!buffList) {
            dev_warn(dev->device, "Command: Failed to allocate DmaBuffer block of %ld bytes\n",
               (ulong)(cnt * sizeof(struct DmaBuffer*)));
            kfree(indexes);
            return -ENOMEM;
         }
         bCnt = 0;

         for (x=0; x < cnt; x++) {
            // Attempt to find buffer in RX list
            if ( (buff = dmaGetBufferList(&(dev->rxBuffers), indexes[x])) != NULL ) {
               // Only return if owned by current desc
               if ( buff->userHas == desc ) {
                  buff->userHas = NULL;
                  buffList[bCnt++] = buff;
               }

            // Attempt to find in tx list
            } else if ( (buff = dmaGetBufferList(&(dev->txBuffers), indexes[x])) != NULL ) {
               // Only return if owned by current desc
               if ( buff->userHas == desc ) {
                  buff->userHas = NULL;

                  // Return entry to TX queue
                  dmaQueuePush(&(dev->tq), buff);
               }
            } else {
               dev_warn(dev->device, "Command: Invalid index posted: %i.\n", indexes[x]);
               kfree(indexes);
               kfree(buffList);
               return -1;
            }
         }

         // Return receive buffers
         dev->hwFunc->retRxBuffer(dev, buffList, bCnt);

         kfree(buffList);
         kfree(indexes);
         return 0;
         break;

      // Request a write buffer index
      case DMA_Get_Index:

         // Read transmit buffer queue
         buff = dmaQueuePop(&(dev->tq));

         // No buffers are available
         if ( buff == NULL ) {
             return -1;
         } else {
            buff->userHas = desc;

            if ( dev->debug > 0 )
               dev_info(dev->device, "Command: Returning buffer %i to user\n", buff->index);
            return buff->index;
         }
         break;

      // Get GIT Version
      case DMA_Get_GITV:
         if (copy_to_user((__user char *)arg, GITV, strnlen(GITV, 32))) {
            return -EFAULT;
         }
         return 0;
         break;

      // Get API Version
      case DMA_Get_Version:
         return DMA_VERSION;
         break;

      // Register write
      case DMA_Write_Register:
         return Dma_WriteRegister(dev, arg);
         break;

      // Register read
      case DMA_Read_Register:
         return Dma_ReadRegister(dev, arg);
         break;

      // All other commands handled by card specific functions
      default:
         return dev->hwFunc->command(dev, cmd, arg);
         break;
   }
   return 0;
}

/**
 * Dma_Poll - Polls DMA queues for readability and writability.
 * @filp: pointer to the file structure
 * @wait: poll table to wait on
 *
 * This function polls DMA queues associated with a DMA descriptor
 * and its device to determine if they are readable or writable.
 * It checks both the device's transmit queue and the descriptor's
 * queue for any pending data.
 *
 * Return: A mask indicating the poll condition. The mask is set
 * to indicate readability (POLLIN | POLLRDNORM) if the descriptor's
 * queue is not empty, and writability (POLLOUT | POLLWRNORM) if the
 * device's transmit queue is not empty.
 */
__poll_t Dma_Poll(struct file *filp, poll_table *wait) {
   struct DmaDesc *desc;
   struct DmaDevice *dev;

   u32 mask = 0;

   desc = (struct DmaDesc *)filp->private_data;
   dev = desc->dev;

   // Polling the device's transmit queue
   dmaQueuePoll(&(dev->tq), filp, wait);
   // Polling the descriptor's queue
   dmaQueuePoll(&(desc->q), filp, wait);

   // Check if the descriptor's queue is not empty (readable)
   if (dmaQueueNotEmpty(&(desc->q)))
      mask |= POLLIN | POLLRDNORM;
   // Check if the device's transmit queue is not empty (writable)
   if (dmaQueueNotEmpty(&(dev->tq)))
      mask |= POLLOUT | POLLWRNORM;

   return (__force __poll_t)mask;
}

/**
 * Dma_Mmap - Map DMA buffers to user space
 * @filp: file pointer
 * @vma: VM area structure
 *
 * This function maps DMA buffers to user space to eliminate a copy if user
 * chooses. It handles both coherent and streaming buffer types as well as
 * ARM ACP, and performs necessary checks on index range, map size, and
 * offset alignment.
 *
 * Return: 0 on success, negative error code on failure.
 */
int Dma_Mmap(struct file *filp, struct vm_area_struct *vma) {
   struct DmaDesc   *desc;
   struct DmaDevice *dev;
   struct DmaBuffer *buff;
   phys_addr_t physical;

   off_t    offset;
   off_t    vsize;
   off_t    base;
   off_t    relMap;
   uint32_t idx;
   int      ret;

   desc = (struct DmaDesc *)filp->private_data;
   dev = desc->dev;

   // Calculate offset and size from vma
   offset = (off_t)vma->vm_pgoff << PAGE_SHIFT;
   vsize = (off_t)vma->vm_end - (off_t)vma->vm_start;

   // Determine buffer index based on offset
   idx = (uint32_t)(offset / dev->cfgSize);

   if (idx < (dev->rxBuffers.count + dev->txBuffers.count)) {
      // Reset offset for mapping
      vma->vm_pgoff = 0;

      // Retrieve buffer based on index
      if ((buff = dmaGetBuffer(dev, idx)) == NULL) {
         dev_warn(dev->device, "map: Invalid index posted: %i.\n", idx);
         return -1;
      }

      // Validate size and alignment
      if ((vsize < dev->cfgSize) || (offset % dev->cfgSize) != 0) {
         dev_warn(dev->device, "map: Invalid map size (%li) and offset (%li). cfgSize=%i\n",
                  vsize, offset, dev->cfgSize);
         return -1;
      }

      // Map coherent buffer
      if (dev->cfgMode & BUFF_COHERENT) {
         ret = dma_mmap_coherent(dev->device, vma, buff->buffAddr, buff->buffHandle, dev->cfgSize);

      // Map streaming buffer or ARM ACP
      } else if (dev->cfgMode & BUFF_STREAM || dev->cfgMode & BUFF_ARM_ACP) {
         ret = io_remap_pfn_range(vma, vma->vm_start,
                               virt_to_phys((void *)buff->buffAddr) >> PAGE_SHIFT,
                               vsize, vma->vm_page_prot);
      } else {
         ret = -1;
      }

      if (ret < 0) {
         dev_warn(dev->device, "map: Failed to map. start 0x%.8lx, end 0x%.8lx, offset %li, size %li, index %i, Ret=%i.\n",
                  vma->vm_start, vma->vm_end, offset, vsize, idx, ret);
      }

      return ret;
   } else {
      // Map register space
      base = (off_t)dev->cfgSize * (dev->rxBuffers.count + dev->txBuffers.count);
      relMap = offset - base;
      physical = dev->baseAddr + relMap;

      // Validate mapping range
      if ((dev->base + relMap) < dev->rwBase) {
         dev_warn(dev->device, "map: Bad map range. start 0x%.8lx, end 0x%.8lx, offset %li, size %li, relMap %li\n",
                  vma->vm_start, vma->vm_end, offset, vsize, relMap);
         return -1;
      }

      dev_warn(dev->device, "map: Mapping offset relMap (0x%lx), physical (0x%llx) with size (%li)\n",
               relMap, physical, vsize);

      ret = io_remap_pfn_range(vma, vma->vm_start, physical >> PAGE_SHIFT, vsize, vma->vm_page_prot);

      if (ret < 0) {
         dev_warn(dev->device, "map: Failed to map. start 0x%.8lx, end 0x%.8lx, offset %li, size %li, relMap %li\n",
                  vma->vm_start, vma->vm_end, offset, vsize, relMap);
         return -1;
      }

      return 0;
   }
}

/**
 * Dma_Fasync - Flush the DMA queue
 * @fd: File descriptor for the DMA device
 * @filp: File pointer to the DMA device file
 * @mode: Fasync mode
 *
 * This function is used to flush the DMA queue. It utilizes the fasync_helper
 * function to manage asynchronous notification.
 *
 * Return: Returns 0 on success, negative error code on failure.
 */
int Dma_Fasync(int fd, struct file *filp, int mode) {
   struct DmaDesc *desc;

   desc = (struct DmaDesc *)filp->private_data;
   return fasync_helper(fd, filp, mode, &(desc->async_queue));
}

/**
 * Dma_ProcOpen - Open the proc file for the DMA device
 * @inode: Inode of the proc file
 * @file: File pointer to the proc file
 *
 * Opens a proc file for the DMA device and sets up the seq_file to point
 * to the device structure. This allows for device-specific information to
 * be output in the proc file system.
 *
 * Return: 0 on success, -1 on failure.
 */
int Dma_ProcOpen(struct inode *inode, struct file *file) {
   struct seq_file *sf;
   struct DmaDevice *dev;

   // PDE_DATA removed in kernel 5.17, backported to RHEL 9.3's 5.14 kernel
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0) || (defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9, 3))
   dev = (struct DmaDevice *)pde_data(inode);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
   dev = (struct DmaDevice *)PDE_DATA(inode);
#else
   dev = (struct DmaDevice *)PDE(inode)->data;
#endif

   if (seq_open(file, &DmaSeqOps) == 0) {
      sf = file->private_data;
      sf->private = dev;
      return 0;
   } else {
      return -1;
   }
}

/**
 * Dma_SeqStart - Start sequence for DMA proc read
 * @s: The seq_file pointer
 * @pos: The start position in the sequence
 *
 * Determines if the sequence iteration should start. If @pos is 0,
 * iteration starts, otherwise it stops.
 *
 * Return: Non-NULL pointer on first iteration, NULL to stop.
 */
void *Dma_SeqStart(struct seq_file *s, loff_t *pos) {
   if (*pos == 0) {
       return (void *)1;
   } else {
       return NULL;
   }
}

/**
 * Dma_SeqNext - Get the next entry in the sequence
 * @s: The seq_file pointer
 * @v: The current entry in the sequence
 * @pos: The current position in the sequence
 *
 * Advances the sequence position. This implementation always returns NULL
 * because there is only one entry to show.
 *
 * Return: Always returns NULL.
 */
void *Dma_SeqNext(struct seq_file *s, void *v, loff_t *pos) {
   (*pos)++;
   return NULL;  // Always return NULL as there is no next item
}

/**
 * Dma_SeqStop - End of sequence
 * @s: The seq_file pointer
 * @v: The current (last) entry in the sequence
 *
 * Cleanup function for ending the sequence. Currently, there's nothing
 * to clean up, so this function does nothing.
 */
void Dma_SeqStop(struct seq_file *s, void *v) {
   // No cleanup required
}

/**
 * Dma_SeqShow - Display DMA buffer statistics.
 * @s: pointer to seq_file.
 * @v: void pointer, unused in this context.
 *
 * This function prints DMA buffer statistics for both read and write
 * operations. It is designed to be called by the seq_file interface
 * to display information about DMA buffers managed by the DMA driver,
 * including usage statistics, buffer counts, and configurations.
 *
 * Return: Always returns 0 on success.
 */
int Dma_SeqShow(struct seq_file *s, void *v) {
   struct   DmaBuffer * buff;
   struct   DmaDevice * dev;
   uint32_t max;
   uint32_t min;
   uint32_t sum;
//   uint32_t avg;
   uint32_t miss;
   uint32_t userCnt;
   uint32_t hwCnt;
   uint32_t hwQCnt;
   uint32_t qCnt;
   uint32_t x;

   dev = (struct DmaDevice *)s->private;

   // Call applications specific show function first
   dev->hwFunc->seqShow(s, dev);

   seq_printf(s, "\n");
   seq_printf(s, "-------- DMA Kernel Driver General --------\n");
   seq_printf(s, " DMA Driver's Git Version : " GITV "\n");
   seq_printf(s, " DMA Driver's API Version : 0x%x\n", DMA_VERSION);
#ifdef DATA_GPU
   seq_printf(s, "         GPUAsync Support : Enabled\n");
#else
   seq_printf(s, "         GPUAsync Support : Disabled\n");
#endif
   seq_printf(s, "\n");
   seq_printf(s, "---- Read Buffers (Firmware->Software) ----\n");
   seq_printf(s, "         Buffer Count : %u\n", dev->rxBuffers.count);
   seq_printf(s, "          Buffer Size : %u\n", dev->cfgSize);
   seq_printf(s, "          Buffer Mode : %u\n", dev->cfgMode);

   userCnt = 0;
   hwCnt   = 0;
   hwQCnt  = 0;
   qCnt    = 0;
   miss    = 0;
// max     = 0;
// min     = 0xFFFFFFFF;
   sum     = 0;

   for (x=dev->rxBuffers.baseIdx; x < (dev->rxBuffers.baseIdx + dev->rxBuffers.count); x++) {
      buff = dmaGetBufferList(&(dev->rxBuffers), x);

   // if ( buff->count > max ) max = buff->count;
   // if ( buff->count < min ) min = buff->count;
      if ( buff->userHas ) userCnt++;
      if (  buff->inHw   && (!buff->inQ) ) hwCnt++;
      if (  buff->inHw   &&  buff->inQ   ) hwQCnt++;
      if ( (!buff->inHw) &&  buff->inQ   ) qCnt++;

      if ( buff->userHas == NULL && buff->inHw == 0 && buff->inQ == 0 ) miss++;

      sum += buff->count;
   }
// if (dev->rxBuffers.count == 0) {
//    min = 0;
//    avg = 0;
// } else {
//    avg = sum/dev->rxBuffers.count;
// }

   seq_printf(s, "      Buffers In User : %u\n", userCnt);
   seq_printf(s, "        Buffers In Hw : %u\n", hwCnt);
   seq_printf(s, "  Buffers In Pre-Hw Q : %u\n", hwQCnt);
   seq_printf(s, "  Buffers In Rx Queue : %u\n", qCnt);
// seq_printf(s, "      Missing Buffers : %u\n", miss);
// seq_printf(s, "       Min Buffer Use : %u\n", min);
// seq_printf(s, "       Max Buffer Use : %u\n", max);
// seq_printf(s, "       Avg Buffer Use : %u\n", avg);
   seq_printf(s, "       Tot Buffer Use : %u\n", sum);

   seq_printf(s, "\n");
   seq_printf(s, "---- Write Buffers (Software->Firmware) ---\n");
   seq_printf(s, "         Buffer Count : %u\n", dev->txBuffers.count);
   seq_printf(s, "          Buffer Size : %u\n", dev->cfgSize);
   seq_printf(s, "          Buffer Mode : %u\n", dev->cfgMode);

   userCnt = 0;
   hwCnt   = 0;
   hwQCnt  = 0;
   qCnt    = 0;
   miss    = 0;
   max     = 0;
   min     = 0xFFFFFFFF;
   sum     = 0;

   for (x=dev->txBuffers.baseIdx; x < (dev->txBuffers.baseIdx + dev->txBuffers.count); x++) {
      buff = dmaGetBufferList(&(dev->txBuffers), x);

      if ( buff->count > max ) max = buff->count;
      if ( buff->count < min ) min = buff->count;
      if ( buff->userHas ) userCnt++;
      if (  buff->inHw   && (!buff->inQ) ) hwCnt++;
      if (  buff->inHw   &&  buff->inQ   ) hwQCnt++;
      if ( (!buff->inHw) &&  buff->inQ   ) qCnt++;

      if ( buff->userHas == NULL && buff->inHw == 0 && buff->inQ == 0 ) miss++;

      sum += buff->count;
   }
// if (dev->txBuffers.count == 0) {
//    min = 0;
//    avg = 0;
// } else {
//    avg = sum/dev->txBuffers.count;
// }

   seq_printf(s, "      Buffers In User : %u\n", userCnt);
   seq_printf(s, "        Buffers In Hw : %u\n", hwCnt);
   seq_printf(s, "  Buffers In Pre-Hw Q : %u\n", hwQCnt);
   seq_printf(s, "  Buffers In Sw Queue : %u\n", qCnt);
// seq_printf(s, "      Missing Buffers : %u\n", miss);
// seq_printf(s, "       Min Buffer Use : %u\n", min);
// seq_printf(s, "       Max Buffer Use : %u\n", max);
// seq_printf(s, "       Avg Buffer Use : %u\n", avg);
   seq_printf(s, "       Tot Buffer Use : %u\n", sum);
   seq_printf(s, "\n");

   return 0;
}

/**
 * Dma_SetMaskBytes - Set the DMA destination mask
 * @dev: pointer to the DMA device structure
 * @desc: pointer to the DMA descriptor
 * @mask: pointer to the mask array
 *
 * This function sets the DMA destination mask for a specific device. It ensures
 * that each destination can only be locked once and that no data is received while
 * the mask flags are being adjusted. If any part of the mask is already set or if
 * the function is called more than once without resetting, it will fail.
 *
 * Return: 0 on success, -1 if the mask is already set or if called more than once.
 */
int Dma_SetMaskBytes(struct DmaDevice *dev, struct DmaDesc *desc, uint8_t *mask) {
   unsigned long iflags;
   uint32_t idx;
   uint32_t destByte;
   uint32_t destBit;

   // Ensure the function is called only once
   static const uint8_t zero[DMA_MASK_SIZE] = {0};
   if (memcmp(desc->destMask, zero, DMA_MASK_SIZE) != 0) return -1;

   // Prevent data reception while adjusting the mask
   spin_lock_irqsave(&dev->maskLock, iflags);

   // Check if all destinations can be locked
   for (idx = 0; idx < DMA_MAX_DEST; idx++) {
      destByte = idx / 8;
      destBit = 1 << (idx % 8);

      // Attempt to lock this destination
      if ((mask[destByte] & destBit) != 0) {
         if (dev->desc[idx] != NULL) {
            spin_unlock_irqrestore(&dev->maskLock, iflags);
            if (dev->debug > 0)
               dev_info(dev->device, "Dma_SetMask: Dest %i already mapped\n", idx);
            return -1;
         }
      }
   }

   // Lock the requested destinations
   for (idx = 0; idx < DMA_MAX_DEST; idx++) {
      destByte = idx / 8;
      destBit = 1 << (idx % 8);

      if ((mask[destByte] & destBit) != 0) {
         dev->desc[idx] = desc;
         if (dev->debug > 0)
            dev_info(dev->device, "Dma_SetMask: Register dest for %i.\n", idx);
      }
   }

   // Update the descriptor's mask
   memcpy(desc->destMask, mask, DMA_MASK_SIZE);

   // Restore interrupts
   spin_unlock_irqrestore(&dev->maskLock, iflags);

   return 0;
}

/**
 * Dma_WriteRegister - Write to a register of the DMA device.
 * @dev: Pointer to the DMA device structure.
 * @arg: User space pointer to data for writing to the register.
 *
 * This function copies a register value from user space and writes it to
 * the specified register within the DMA device. It checks for valid address
 * ranges and reports errors if copy_from_user fails or if the address is out
 * of range.
 *
 * Return: 0 on success, or -1 on failure.
 */
int32_t Dma_WriteRegister(struct DmaDevice *dev, uint64_t arg) {
   uint64_t ret;
   struct DmaRegisterData rData;

   // Attempt to copy register data from user space
   ret = copy_from_user(&rData, (__user void *)arg, sizeof(struct DmaRegisterData));
   if (ret) {
      dev_warn(dev->device, "Dma_WriteRegister: copy_from_user failed. ret=%llu, user=%p kern=%p\n",
               ret, (__user void *)arg, &rData);
      return -1;
   }

   // Validate register address range
   if (((dev->base + rData.address) < dev->rwBase) ||
       ((dev->base + rData.address + 4) > (dev->rwBase + dev->rwSize))) {
      return -1;
   }

   // Write data to the register
   writel(rData.data, dev->base + rData.address);

   return 0;
}

/**
 * Dma_ReadRegister - Read a register from a DMA device.
 * @dev: pointer to the DmaDevice structure.
 * @arg: user space pointer to DmaRegisterData structure where the read data will be stored.
 *
 * This function reads a register from the DMA device specified by @dev,
 * using the address specified in the @arg user space DmaRegisterData structure.
 * The read value is then written back to the same structure in user space.
 * If any step fails, the function returns an error.
 *
 * Return: 0 on success, -1 on failure.
 */
int32_t Dma_ReadRegister(struct DmaDevice *dev, uint64_t arg) {
   uint64_t ret;
   struct DmaRegisterData rData;

   // Attempt to copy DmaRegisterData structure from user space
   if ((ret = copy_from_user(&rData, (__user void *)arg, sizeof(struct DmaRegisterData)))) {
      dev_warn(dev->device, "Dma_ReadRegister: copy_from_user failed. ret=%llu, user=%p kern=%p\n", ret, (void *)arg, &rData);
      return -1;
   }

   // Validate register address within the allowed range
   if (((dev->base + rData.address) < dev->rwBase) ||
       ((dev->base + rData.address + 4) > (dev->rwBase + dev->rwSize))) {
      return -1;
   }

   // Read register value
   rData.data = readl(dev->base + rData.address);

   // Attempt to copy the updated DmaRegisterData structure back to user space
   if ((ret = copy_to_user((__user void *)arg, &rData, sizeof(struct DmaRegisterData)))) {
      dev_warn(dev->device, "Dma_ReadRegister: copy_to_user failed. ret=%llu, user=%p kern=%p\n", ret, (__user void *)arg, &rData);
      return -1;
   }

   return 0;
}
