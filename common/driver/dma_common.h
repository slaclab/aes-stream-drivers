/**
 *-----------------------------------------------------------------------------
 * Title      : Common access functions, not card specific
 * ----------------------------------------------------------------------------
 * File       : dma_common.h
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
#ifndef __DMA_COMMON_H__
#define __DMA_COMMON_H__

#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <DmaDriver.h>
#include <dma_buffer.h>

// Max number of devices to support
#define MAX_DMA_DEVICES 4

// Maximum number of channels
#define DMA_MAX_DEST (8*DMA_MASK_SIZE)

// Forward declarations
struct hardware_functions;
struct DmaDesc;

// Device structure
struct DmaDevice {

   // PCI address regions
   phys_addr_t baseAddr;
   uint32_t    baseSize;

   // Base pointer to memory region
   uint8_t * base;

   // Register pointers, may be the same as reg
   void * reg; // hardware specific

   // Direct read/write offset and size
   uint8_t * rwBase;
   uint32_t  rwSize;

   // Configuration
   uint32_t cfgSize;
   uint32_t cfgTxCount;
   uint32_t cfgRxCount;
   uint32_t cfgMode;
   uint32_t cfgCont;

   // Device tracking
   uint32_t        index;
   uint32_t        major;
   dev_t           devNum;
   char            devName[50];
   struct cdev     charDev;
   struct device * device;

   // Card Info
   struct hardware_functions * hwFunc;
   uint8_t destMask[DMA_MASK_SIZE];
   void *  hwData;

   // Debug flag
   uint8_t debug;

   // IRQ
   uint32_t irq;

   // Locks
   spinlock_t writeHwLock;
   spinlock_t commandLock;
   spinlock_t maskLock;

   // Owners
   struct DmaDesc * desc[DMA_MAX_DEST];

   // Transmit/receive buffer list
   struct DmaBufferList txBuffers;
   struct DmaBufferList rxBuffers;

   // Transmit queue
   struct DmaQueue tq;
};

// File descriptor struct
struct DmaDesc {

   // Mask of destinations
   uint8_t destMask[DMA_MASK_SIZE];

   // Receive queue
   struct DmaQueue q;

   // Async queue
   struct fasync_struct *async_queue;   

   // Pointer back to card structure
   struct DmaDevice * dev;
};

// Hardware Functions
struct hardware_functions {
   irqreturn_t (*irq)(int irq, void *dev_id);
   void        (*init)(struct DmaDevice *dev);
   void        (*clear)(struct DmaDevice *dev);
   void        (*retRxBuffer)(struct DmaDevice *dev, struct DmaBuffer *buff);
   int32_t     (*sendBuffer)(struct DmaDevice *dev, struct DmaBuffer *buff);
   int32_t     (*command)(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);
   void        (*seqShow)(struct seq_file *s, struct DmaDevice *dev);
   int32_t     (*promRead)(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);
};

// Global array of devices
extern struct DmaDevice gDmaDevices[MAX_DMA_DEVICES];

// Number of active devices
extern uint32_t gDmaDevCount;

// Global variable for the device class 
extern struct class * gCl;

// Function structure for below functions
extern struct file_operations DmaFunctions;

////////////////////////////////////////////
// Functions below
////////////////////////////////////////////

// Devnode callback to set permissions of created devices
char *Dma_DevNode(struct device *dev, umode_t *mode);

// Map dma registers
int Dma_MapReg ( struct DmaDevice *dev );

// Create and init device
int Dma_Init(struct DmaDevice *dev);

// Cleanup device
void Dma_Clean(struct DmaDevice *dev);

// Open Returns 0 on success, error code on failure
int Dma_Open(struct inode *inode, struct file *filp);

// Dma_Release
// Called when the device is closed
// Returns 0 on success, error code on failure
int Dma_Release(struct inode *inode, struct file *filp);

// Dma_Read
// Called when the device is read from
// Returns read count on success. Error code on failure.
ssize_t Dma_Read(struct file *filp, char *buffer, size_t count, loff_t *f_pos);

// Dma_Write
// Called when the device is written to
// Returns write count on success. Error code on failure.
ssize_t Dma_Write(struct file *filp, const char* buffer, size_t count, loff_t* f_pos);

// Perform commands
ssize_t Dma_Ioctl(struct file *filp, uint32_t cmd, unsigned long arg);

// Poll/Select
uint32_t Dma_Poll(struct file *filp, poll_table *wait );

// Memory map
// This needs to be redone
int Dma_Mmap(struct file *filp, struct vm_area_struct *vma);

// Flush queue
int Dma_Fasync(int fd, struct file *filp, int mode);

// Open proc file
int Dma_ProcOpen(struct inode *inode, struct file *file);

// Sequence start
void * Dma_SeqStart(struct seq_file *s, loff_t *pos);

// Sequence start
void * Dma_SeqNext(struct seq_file *s, void *v, loff_t *pos);

// Sequence end
void Dma_SeqStop(struct seq_file *s, void *v);

// Sequence show
int Dma_SeqShow(struct seq_file *s, void *v);

// Set Mask
int Dma_SetMaskBytes(struct DmaDevice *dev, struct DmaDesc *desc, uint8_t * mask );

// Write Register
int32_t Dma_WriteRegister(struct DmaDevice *dev, uint64_t arg);

// Prom write 
int32_t Dma_ReadRegister(struct DmaDevice *dev, uint64_t arg);

#endif

