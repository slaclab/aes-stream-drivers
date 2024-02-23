/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    This header file defines the interface and structures for managing DMA
 *    buffers within the aes_stream_drivers package. It provides a comprehensive
 *    API for allocating, deallocating, and manipulating DMA buffers that are
 *    essential for high-throughput data transfers between the CPU and peripheral
 *    devices in a system. The functionality encapsulated by this file is crucial
 *    for achieving efficient direct memory access operations, reducing CPU load,
 *    and enhancing overall data transfer performance within kernel-space drivers.
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

/**
 * Buffer Modes
 *
 * Defines primary buffer modes as bits to enable application-specific expansion.
 */
#define BUFF_COHERENT  0x1
#define BUFF_STREAM    0x2
#define BUFF_ARM_ACP   0x4

/**
 * Number of buffers per list.
 */
#define BUFFERS_PER_LIST 100000

// Forward declarations
struct DmaDevice;
struct DmaDesc;
struct DmaBufferList;

/**
 * struct DmaBuffer - TX/RX Buffer
 * @index: Internal tracking index.
 * @count: Usage count of the buffer.
 * @userHas: Pointer to user descriptor.
 * @inHw: Flag indicating if the buffer is in hardware.
 * @inQ: Flag indicating if the buffer is queued.
 * @owner: Ownership flag.
 * @dest: Destination identifier.
 * @flags: Buffer flags.
 * @error: Error status.
 * @size: Size of the buffer.
 * @id: Buffer identifier.
 * @buffList: Pointer to the buffer list containing this buffer.
 * @buffAddr: Virtual address of the buffer.
 * @buffHandle: DMA handle for the buffer.
 *
 * Represents a buffer for transmitting or receiving data, including metadata
 * for management and tracking.
 */
struct DmaBuffer {
   uint32_t         index;
   uint32_t         count;
   struct DmaDesc * userHas;
   uint8_t          inHw;
   uint8_t          inQ;
   uint8_t          owner;
   uint16_t         dest;
   uint32_t         flags;
   uint8_t          error;
   uint32_t         size;
   uint32_t         id;
   struct DmaBufferList * buffList;
   void      * buffAddr;
   dma_addr_t  buffHandle;
};

/**
 * struct DmaBufferList - Buffer List
 * @baseIdx: Base index of the buffers in the list.
 * @direction: DMA transfer direction.
 * @dev: Associated DMA device.
 * @indexed: Indexed list of buffers.
 * @sorted: Sorted list of buffers for efficient search.
 * @subCount: Number of sub-lists.
 * @count: Total number of buffers in the list.
 *
 * Organizes multiple DmaBuffers for efficient access and management, supporting
 * operations like allocation, deallocation, and sorting.
 */
struct DmaBufferList {
   uint32_t baseIdx;
   enum dma_data_direction direction;
   struct DmaDevice * dev;
   struct DmaBuffer *** indexed;
   struct DmaBuffer ** sorted;
   uint32_t subCount;
   uint32_t count;
};

/**
 * struct DmaQueue - DMA Queue
 * @count: Total count of buffers in the queue.
 * @subCount: Number of sub-queues.
 * @queue: Array of queue entries.
 * @read: Read pointer index.
 * @write: Write pointer index.
 * @lock: Spinlock for concurrent access protection.
 * @wait: Wait queue for blocking operations.
 *
 * Represents a queue for DMA buffers, facilitating ordered processing and
 * synchronization between producers and consumers.
 */
struct DmaQueue {
   uint32_t count;
   uint32_t subCount;
   struct DmaBuffer ***queue;
   uint32_t read;
   uint32_t write;
   spinlock_t lock;
   wait_queue_head_t wait;
};

// Function prototypes
size_t dmaAllocBuffers(struct DmaDevice *dev, struct DmaBufferList *list, uint32_t count, uint32_t baseIdx, enum dma_data_direction direction);
void dmaFreeBuffersList(struct DmaBufferList *list);
void dmaFreeBuffers(struct DmaBufferList *list);
void *bsearch(const void *key, const void *base, size_t num, size_t size, int (*cmp)(const void *key, const void *elt));
int32_t dmaSortComp(const void *p1, const void *p2);
int32_t dmaSearchComp(const void *key, const void *element);
struct DmaBuffer *dmaFindBufferList(struct DmaBufferList *list, dma_addr_t handle);
struct DmaBuffer *dmaFindBuffer(struct DmaDevice *dev, dma_addr_t handle);
struct DmaBuffer *dmaGetBufferList(struct DmaBufferList *list, uint32_t index);
struct DmaBuffer *dmaGetBuffer(struct DmaDevice *dev, uint32_t index);
struct DmaBuffer *dmaRetBufferIrq(struct DmaDevice *device, dma_addr_t handle);
struct DmaBuffer *dmaRetBufferIdx(struct DmaDevice *device, uint32_t index);
struct DmaBuffer *dmaRetBufferIdxIrq(struct DmaDevice *device, uint32_t index);
void dmaRxBuffer(struct DmaDesc *desc, struct DmaBuffer *buff);
void dmaRxBufferIrq(struct DmaDesc *desc, struct DmaBuffer *buff);
void dmaSortBuffers(struct DmaBufferList *list);
int32_t dmaBufferToHw(struct DmaBuffer *buff);
void dmaBufferFromHw(struct DmaBuffer *buff);
size_t dmaQueueInit(struct DmaQueue *queue, uint32_t count);
void dmaQueueFree(struct DmaQueue *queue);
uint32_t dmaQueueNotEmpty(struct DmaQueue *queue);
uint32_t dmaQueuePush(struct DmaQueue *queue, struct DmaBuffer *entry);
uint32_t dmaQueuePushIrq(struct DmaQueue *queue, struct DmaBuffer *entry);
uint32_t dmaQueuePushList(struct DmaQueue *queue, struct DmaBuffer **buff, size_t cnt);
uint32_t dmaQueuePushListIrq(struct DmaQueue *queue, struct DmaBuffer **buff, size_t cnt);
struct DmaBuffer *dmaQueuePop(struct DmaQueue *queue);
struct DmaBuffer *dmaQueuePopIrq(struct DmaQueue *queue);
ssize_t dmaQueuePopList(struct DmaQueue *queue, struct DmaBuffer **buff, size_t cnt);
ssize_t dmaQueuePopListIrq(struct DmaQueue *queue, struct DmaBuffer **buff, size_t cnt);
void dmaQueuePoll(struct DmaQueue *queue, struct file *filp, poll_table *wait);
void dmaQueueWait(struct DmaQueue *queue);

#endif // __DMA_BUFFER_H__
