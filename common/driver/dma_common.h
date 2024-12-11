/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    This header file defines the interface and structures for Direct
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

#ifndef __DMA_COMMON_H__
#define __DMA_COMMON_H__

#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <DmaDriver.h>
#include <dma_buffer.h>

// Maximum number of destination channels
#define DMA_MAX_DEST (8*DMA_MASK_SIZE)

// Forward declarations
struct hardware_functions;
struct DmaDesc;

/**
 * struct DmaDevice - Represents a DMA-capable device.
 * @baseAddr: Base physical address of the device's memory-mapped I/O region.
 * @baseSize: Size of the memory-mapped I/O region.
 * @base: Base virtual address pointer to the device's memory region.
 * @reg: Virtual address pointer to the device's register set.
 * @rwBase: Direct read/write virtual address within the device memory.
 * @rwSize: Size of the direct read/write region.
 * @cfgSize: Size of the device configuration space.
 * @cfgTxCount: Transmit buffer count configuration.
 * @cfgRxCount: Receive buffer count configuration.
 * @cfgMode: Operating mode configuration.
 * @cfgCont: Continuous mode setting.
 * @cfgIrqHold: IRQ hold-off configuration.
 * @cfgBgThold: Background threshold configuration (array of 8 values).
 * @cfgIrqDis: IRQ disable flag.
 * @index: Device index.
 * @major: Major number assigned to the device.
 * @devNum: Device number.
 * @devName: Device name.
 * @charDev: Character device structure.
 * @device: Generic device structure.
 * @pcidev: Associated PCI device structure.
 * @hwFunc: Pointer to hardware-specific functions.
 * @destMask: Destination mask for DMA operations.
 * @hwData: Hardware-specific data.
 * @utilData: Utility data for driver use.
 * @debug: Debug flag.
 * @irq: IRQ number.
 * @writeHwLock: Spinlock for hardware write operations.
 * @commandLock: Spinlock for command operations.
 * @maskLock: Spinlock for destination mask operations.
 * @desc: Array of pointers to descriptor structures for DMA channels.
 * @txBuffers: List of transmit buffers.
 * @rxBuffers: List of receive buffers.
 * @tq: Transmit queue structure.
 *
 * This structure defines a DMA device, including its configuration,
 * memory regions, buffer management, and associated locks.
 */

struct DmaDevice {
   // PCI address regions
   phys_addr_t baseAddr;
   uint32_t    baseSize;

   // Base pointer to memory region
   uint8_t * base;

   // Register pointers, may be the same as reg
   void * reg;  // hardware specific

   // Direct read/write offset and size
   uint8_t * rwBase;
   uint32_t  rwSize;

   // Configuration
   uint32_t cfgSize;
   uint32_t cfgTxCount;
   uint32_t cfgRxCount;
   uint32_t cfgMode;
   uint32_t cfgCont;
   uint32_t cfgIrqHold;
   uint32_t cfgBgThold[8];
   uint32_t cfgIrqDis;

   // Device tracking
   uint32_t        index;
   uint32_t        major;
   dev_t           devNum;
   char            devName[50];
   struct cdev     charDev;
   struct device * device;
   struct pci_dev *pcidev;

   // Card Info
   struct hardware_functions * hwFunc;
   uint8_t destMask[DMA_MASK_SIZE];
   void *  hwData;
   void *  utilData;

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

/**
 * struct DmaDesc - DMA descriptor for a device.
 * @destMask: Destination mask for DMA transfers.
 * @q: Receive queue for the descriptor.
 * @async_queue: Asynchronous notification queue.
 * @dev: Back-pointer to the associated DmaDevice.
 *
 * This structure represents a DMA descriptor, which is used to manage
 * DMA transfers for a specific destination or set of destinations.
 */
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

/**
 * struct hardware_functions - Hardware-specific functions for a DMA device.
 * @irq: IRQ handler function.
 * @init: Initialization function.
 * @enable: Enable operation function.
 * @clear: Clear operation function.
 * @retRxBuffer: Return received buffer function.
 * @sendBuffer: Send buffer function.
 * @command: Command execution function.
 * @seqShow: Function to display device information in a sequential file.
 *
 * This structure defines a set of hardware-specific operations that are
 * required to manage a DMA device. It includes functions for initialization,
 * buffer management, command processing, and debugging.
 */
struct hardware_functions {
   irqreturn_t (*irq)(int irq, void *dev_id);
   void        (*init)(struct DmaDevice *dev);
   void        (*enable)(struct DmaDevice *dev);
   void        (*clear)(struct DmaDevice *dev);
   void        (*retRxBuffer)(struct DmaDevice *dev, struct DmaBuffer **buff, uint32_t count);
   int32_t     (*sendBuffer)(struct DmaDevice *dev, struct DmaBuffer **buff, uint32_t count);
   int32_t     (*command)(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);
   void        (*seqShow)(struct seq_file *s, struct DmaDevice *dev);
};

// Global array of devices
extern struct DmaDevice gDmaDevices[];

// Number of active devices
extern uint32_t gDmaDevCount;

// Global variable for the device class
extern struct class * gCl;

// Function structure for below functions
extern struct file_operations DmaFunctions;

// Function prototypes
char *Dma_DevNode(struct device *dev, umode_t *mode);
int Dma_MapReg(struct DmaDevice *dev);
int Dma_Init(struct DmaDevice *dev);
void Dma_Clean(struct DmaDevice *dev);
int Dma_Open(struct inode *inode, struct file *filp);
int Dma_Release(struct inode *inode, struct file *filp);
ssize_t Dma_Read(struct file *filp, char *buffer, size_t count, loff_t *f_pos);
ssize_t Dma_Write(struct file *filp, const char* buffer, size_t count, loff_t* f_pos);
ssize_t Dma_Ioctl(struct file *filp, uint32_t cmd, unsigned long arg);
uint32_t Dma_Poll(struct file *filp, poll_table *wait);
int Dma_Mmap(struct file *filp, struct vm_area_struct *vma);
int Dma_Fasync(int fd, struct file *filp, int mode);
int Dma_ProcOpen(struct inode *inode, struct file *file);
void * Dma_SeqStart(struct seq_file *s, loff_t *pos);
void * Dma_SeqNext(struct seq_file *s, void *v, loff_t *pos);
void Dma_SeqStop(struct seq_file *s, void *v);
int Dma_SeqShow(struct seq_file *s, void *v);
int Dma_SetMaskBytes(struct DmaDevice *dev, struct DmaDesc *desc, uint8_t * mask);
int32_t Dma_WriteRegister(struct DmaDevice *dev, uint64_t arg);
int32_t Dma_ReadRegister(struct DmaDevice *dev, uint64_t arg);
void Dma_UnmapReg(struct DmaDevice *dev);

#endif  // __DMA_COMMON_H__
