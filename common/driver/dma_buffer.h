/**
 *-----------------------------------------------------------------------------
 * Title      : General purpose DMA buffers.
 * ----------------------------------------------------------------------------
 * File       : dma_buffer.h
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * General purpose DMA buffers for drivers.
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
#ifndef __DMA_BUFFER_H__
#define __DMA_BUFFER_H__

#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>

// Buffer modes
// Primary modes are bits to enable app specific expansion
#define BUFF_COHERENT  0x1
#define BUFF_STREAM    0x2
#define BUFF_ARM_ACP   0x4

// Forward declaration
struct DmaDevice;
struct DmaDesc;
struct DmaBufferList;

// TX/RX Buffer
struct DmaBuffer {

   // Internal tracking
   uint32_t         index;
   uint32_t         count;
   struct DmaDesc * userHas;
   uint8_t          inHw;
   uint8_t          inQ;
   uint8_t          owner;

   // Associated data
   uint8_t     dest;
   uint32_t    flags;
   uint8_t     error;
   uint32_t    size;

   // Pointers
   struct DmaBufferList * buffList;
   void      * buffAddr;
   dma_addr_t  buffHandle;

};

// Buffer List
struct DmaBufferList {

   // Base index
   uint32_t baseIdx;

   // Buffer direction
   enum dma_data_direction direction;

   // Associated device
   struct DmaDevice * dev;

   // Buffer list
   struct DmaBuffer ** indexed;

   // Sorted buffer list
   struct DmaBuffer ** sorted;

   // Number of buffers in list
   uint32_t count;
};

// DMA Queue
struct DmaQueue {
   uint32_t count;

   // Entries
   struct DmaBuffer **queue;

   // Read and write pointers
   uint32_t read;
   uint32_t write;

   // Access lock
   spinlock_t lock;

   // Queue wait
   wait_queue_head_t wait;
};

// Create a list of buffer
// Return number of buffers created
size_t dmaAllocBuffers ( struct DmaDevice *dev, struct DmaBufferList *list,
                         uint32_t count, uint32_t baseIdx, enum dma_data_direction direction);

// Free a list of buffer
void dmaFreeBuffers ( struct DmaBufferList *list );

// Binary search
void *bsearch(const void *key, const void *base, size_t num, size_t size,
	      int (*cmp)(const void *key, const void *elt));

// Buffer comparison for sort
// Return comparison result, 1 if greater, -1 if less, 0 if equal
int32_t dmaSortComp (const void *p1, const void *p2);

// Buffer comparison for search
// Return comparison result, 1 if greater, -1 if less, 0 if equal
int32_t dmaSearchComp (const void *key, const void *element);

// Find a buffer from specific list
struct DmaBuffer * dmaFindBufferList ( struct DmaBufferList *list, dma_addr_t handle );

// Find a buffer from either list
struct DmaBuffer * dmaFindBuffer ( struct DmaDevice *dev, dma_addr_t handle );

// Get a buffer using index, in passed list
struct DmaBuffer * dmaGetBufferList ( struct DmaBufferList *list, uint32_t index );

// Get a buffer using index, in either list
struct DmaBuffer * dmaGetBuffer ( struct DmaDevice *dev, uint32_t index );

// Conditionally return buffer to transmit buffer. If buffer is not found in 
// transmit list return a pointer to the buffer. Passed value is the dma handle.
struct DmaBuffer * dmaRetBufferIrq ( struct DmaDevice *device, dma_addr_t handle );

// Conditionally return buffer to transmit buffer. If buffer is not found in 
// transmit list return a pointer to the buffer. Passed value is the dma handle.
struct DmaBuffer * dmaRetBufferIdxIrq ( struct DmaDevice *device, uint32_t index );

// Push buffer to descriptor receive queue
void dmaRxBuffer ( struct DmaDesc *desc, struct DmaBuffer *buff );

// Sort a buffer list
void dmaSortBuffers ( struct DmaBufferList *list );

// Buffer being passed to hardware
int32_t dmaBufferToHw ( struct DmaBuffer *buff);

// Buffer being returned from hardware
void dmaBufferFromHw ( struct DmaBuffer *buff);

// Init queue
// Return number initialized
size_t dmaQueueInit ( struct DmaQueue *queue, uint32_t count );

// Free queue
void dmaQueueFree ( struct DmaQueue *queue );

// Dma queue is not empty
// Return 0 if empty, 1 if not empty
uint32_t dmaQueueNotEmpty ( struct DmaQueue *queue );

// Push a queue entry
// Return 1 if fail, 0 if success
// Use IRQ method inside of IRQ handler
uint32_t dmaQueuePush    ( struct DmaQueue *queue, struct DmaBuffer *entry );
uint32_t dmaQueuePushIrq ( struct DmaQueue *queue, struct DmaBuffer *entry );

// Pop a queue entry
// Return a queue entry, NULL if nothing available
// Use IRQ method inside of IRQ handler
struct DmaBuffer * dmaQueuePop ( struct DmaQueue *queue );

// Poll queue
void dmaQueuePoll ( struct DmaQueue *queue, struct file *filp, poll_table *wait );

// Wait on queue
void dmaQueueWait ( struct DmaQueue *queue );

#endif

