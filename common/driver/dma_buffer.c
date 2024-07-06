/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    This file implements the management and operations of general-purpose Direct
 *    Memory Access (DMA) buffers used within the driver framework. These buffers
 *    facilitate efficient data transfers between the CPU and hardware peripherals
 *    without CPU intervention, optimizing performance for high-speed data
 *    processing and transfer tasks.
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

#include <asm/io.h>
#include <linux/dma-mapping.h>
#include <linux/sort.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/version.h>

#include <dma_buffer.h>
#include <dma_common.h>

/**
 * dmaAllocBuffers - Allocate DMA buffers and organize them into a list
 * @dev: pointer to the DMA device structure
 * @list: pointer to the DMA buffer list structure to populate
 * @count: number of buffers to allocate
 * @baseIdx: base index for buffer enumeration
 * @direction: the DMA data transfer direction
 *
 * This function allocates a specified number of DMA buffers and organizes them
 * into a list for management. It handles different buffer types based on the
 * device configuration, including coherent, streaming, and ACP (ARM Coherent Port).
 * It returns the number of successfully allocated buffers, which can be less than
 * requested in case of allocation failures.
 *
 * Return: the count of successfully allocated buffers, or 0 on failure.
 */
size_t dmaAllocBuffers ( struct DmaDevice *dev, struct DmaBufferList *list,
                         uint32_t count, uint32_t baseIdx, enum dma_data_direction direction) {
   uint32_t x;
   uint32_t sl;
   uint32_t sli;

   struct DmaBuffer * buff;

   // Determine number of sub-lists
   list->subCount = (count / BUFFERS_PER_LIST) + 1;

   list->indexed   = NULL;
   list->sorted    = NULL;
   list->count     = 0;
   list->direction = direction;
   list->dev       = dev;
   list->baseIdx   = baseIdx;

   if ( count == 0 ) return 0;

   // Allocate first level pointers
   if ((list->indexed = (struct DmaBuffer ***) kzalloc(sizeof(struct DmaBuffer**) * list->subCount, GFP_KERNEL)) == NULL ) {
      dev_err(dev->device,"dmaAllocBuffers: Failed to allocate indexed list pointer. Count=%u.\n",list->subCount);
      goto cleanup_forced_exit;
   }

   // Allocate sub lists
   for (x=0; x < list->subCount; x++) {
      if ((list->indexed[x] = (struct DmaBuffer **) kzalloc((sizeof(struct DmaBuffer *) * BUFFERS_PER_LIST), GFP_KERNEL)) == NULL) {
         dev_err(dev->device,"dmaAllocBuffers: Failed to allocate sub list. Idx=%u.\n",x);
         goto cleanup_list_heads;
      }
   }

   // Sorted lists are not always available. Disable for streaming mode or when we have too many buffers for
   // a single sorted list
   if ( (list->subCount == 1) && ((list->dev->cfgMode & BUFF_STREAM) == 0) ) {
      list->sorted = (struct DmaBuffer **) kzalloc(sizeof(struct DmaBuffer**) * count, GFP_KERNEL);
   }

   // Allocate buffers
   for (x=0; x < count; x++) {
      sl  = x / BUFFERS_PER_LIST;
      sli = x % BUFFERS_PER_LIST;
      if ( (buff = (struct DmaBuffer *) kzalloc(sizeof(struct DmaBuffer), GFP_KERNEL)) == NULL) {
         dev_err(dev->device,"dmaAllocBuffers: Failed to create buffer structure index %ui. Unloading.\n",x);
         goto cleanup_buffers;
      }

      // Init record
      memset(buff,0,sizeof(struct DmaBuffer));

      // Setup pointer back to list
      buff->buffList = list;

      // Coherent buffer, map dma coherent buffers
      if ( list->dev->cfgMode & BUFF_COHERENT ) {
         buff->buffAddr =
            dma_alloc_coherent(list->dev->device, list->dev->cfgSize, &(buff->buffHandle), GFP_DMA | GFP_KERNEL);

      // Streaming buffer type, standard kernel memory
      } else if ( list->dev->cfgMode & BUFF_STREAM ) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
         buff->buffAddr = kmalloc(list->dev->cfgSize, GFP_KERNEL);
         if (buff->buffAddr != NULL) {
            buff->buffHandle = dma_map_single(list->dev->device, buff->buffAddr, list->dev->cfgSize, direction);
            // Check for mapping error
            if ( dma_mapping_error(list->dev->device,buff->buffHandle) ) {
               // DMA mapping was successful
               buff->buffHandle = 0;
            } else {
               // DMA mapping failed
               dev_err(dev->device, "dmaAllocBuffers(BUFF_STREAM): DMA mapping failed\n");
            }
         } else {
            // Memory allocation failed
            dev_err(list->dev->device, "dmaAllocBuffers(BUFF_STREAM): kmalloc Memory allocation failed\n");
         }
#else
         buff->buffAddr = dma_alloc_pages(list->dev->device, list->dev->cfgSize, &buff->buffHandle, direction, GFP_KERNEL);
         // Check for mapping error
         if (buff->buffAddr == NULL) {
            dev_err(dev->device, "dmaAllocBuffers(BUFF_STREAM): dma_alloc_pages failed\n");
         }
#endif

      // ACP type with permanent handle mapping, dma capable kernel memory
      } else if ( list->dev->cfgMode & BUFF_ARM_ACP ) {
         buff->buffAddr = kzalloc(list->dev->cfgSize, GFP_DMA | GFP_KERNEL);
         if (buff->buffAddr != NULL)
            buff->buffHandle = virt_to_phys(buff->buffAddr);
      }

      // Alloc or mapping failed
      if ( buff->buffAddr == NULL || buff->buffHandle == 0) {
         dev_err(dev->device,"dmaAllocBuffers: Failed to create stream buffer and dma mapping.\n");
         goto cleanup_buffers;
      }

      // Set index
      buff->index = x + list->baseIdx;
      list->indexed[sl][sli] = buff;

      // Populate entry in sorted list for later sort
      if ( list->sorted != NULL ) list->sorted[sli] = buff;
      list->count++;
   }

   // Sort the buffers
   if ( list->sorted != NULL ) sort(list->sorted,list->count,sizeof(struct DmaBuffer *),dmaSortComp,NULL);

   return list->count;

   /* Cleanup */
cleanup_buffers:
   dmaFreeBuffersList(list);
   if ( list->sorted  != NULL ) kfree(list->sorted);

cleanup_list_heads:
   for (x=0; x < list->subCount; x++)
      if ( list->indexed[x] != NULL )
         kfree(list->indexed[x]);
   if ( list->indexed != NULL ) kfree(list->indexed);

// Return 0 as no buffers were allocated
cleanup_forced_exit:
   return 0;
}

/**
 * dmaFreeBuffersList - Free a list of DMA buffers.
 * @list: pointer to the structure DmaBufferList representing the list of buffers.
 *
 * This function iterates through a list of DMA buffers and frees each buffer
 * according to its type (coherent, streaming, or ARM ACP). It supports different
 * freeing mechanisms based on the buffer type, including dma_free_coherent for
 * coherent buffers and dma_unmap_single for streaming buffers. The function also
 * resets the buffer count to 0 after all buffers have been freed.
 */
void dmaFreeBuffersList(struct DmaBufferList *list) {
   uint32_t sl;
   uint32_t sli;
   uint32_t x;

   for (x = 0; x < list->count; x++) {
      sl  = x / BUFFERS_PER_LIST;
      sli = x % BUFFERS_PER_LIST;

      if (list->indexed[sl][sli]->buffAddr != NULL) {

         // Free coherent buffer
         if (list->dev->cfgMode & BUFF_COHERENT) {
            dma_free_coherent(list->dev->device, list->dev->cfgSize,
                              list->indexed[sl][sli]->buffAddr,
                              list->indexed[sl][sli]->buffHandle);
         }

         // Unmap streaming buffer
         if (list->dev->cfgMode & BUFF_STREAM) {
            dma_unmap_single(list->dev->device,
                             list->indexed[sl][sli]->buffHandle,
                             list->dev->cfgSize, list->direction);
         }

         // Free buffer for streaming type or ARM ACP
         if ((list->dev->cfgMode & BUFF_STREAM) || (list->dev->cfgMode & BUFF_ARM_ACP)) {
            kfree(list->indexed[sl][sli]->buffAddr);
         }
      }
      // Free the buffer structure
      kfree(list->indexed[sl][sli]);
   }
   // Reset buffer count
   list->count = 0;
}

/**
 * dmaFreeBuffers - Free a list of DMA buffers including head buffers
 * @list: pointer to the DmaBufferList structure to be freed
 *
 * This function frees all buffers associated with a DMA buffer list,
 * including the head of the list and any indexed or sorted buffers.
 * It also calls dmaFreeBuffersList to handle list-specific deallocations.
 */
void dmaFreeBuffers(struct DmaBufferList *list) {
   uint32_t x;

   // Free the list itself
   dmaFreeBuffersList(list);

   // Free each buffer in the indexed array
   for (x = 0; x < list->subCount; x++)
      kfree(list->indexed[x]);

   // Free the indexed and sorted arrays if they are not NULL
   if (list->indexed != NULL)
      kfree(list->indexed);
   if (list->sorted != NULL)
      kfree(list->sorted);
}

/**
 * bsearch - Perform a binary search on a sorted array.
 * @key: pointer to the item being searched for
 * @base: pointer to the first element in the array
 * @num: number of elements in the array
 * @size: size of each element in the array
 * @cmp: comparator function which returns negative if the first argument is less
 *       than the second, zero if they're equal, and positive if the first
 *       argument is greater than the second
 *
 * This function performs a binary search on a sorted array. It assumes that
 * the array is sorted in ascending order according to the comparator function.
 * The function returns a pointer to the matching element if found, or NULL if
 * not found.
 *
 * Return: On success, returns a pointer to the matching element. If no match
 *         is found, returns NULL.
 */
void *bsearch(const void *key, const void *base, size_t num, size_t size,
              int (*cmp)(const void *key, const void *elt)) {
   int start = 0, end = num - 1, mid, result;

   if (num == 0) return NULL;

   while (start <= end) {
      mid = (start + end) / 2;
      result = cmp(key, (const char *)base + mid * size);
      if (result < 0)
         end = mid - 1;
      else if (result > 0)
         start = mid + 1;
      else
         return (void *)((const char *)base + mid * size);
   }
   return NULL;
}

/**
 * dmaSortComp - Compare two DMA buffers for sorting.
 * @p1: pointer to the first DMA buffer.
 * @p2: pointer to the second DMA buffer.
 *
 * This function compares two DMA buffers based on their buffer handles
 * to determine their order. It's used primarily for sorting purposes.
 *
 * Return: 1 if the first buffer is greater, -1 if less, and 0 if equal.
 */
int32_t dmaSortComp(const void *p1, const void *p2) {
   struct DmaBuffer **b1 = (struct DmaBuffer **)p1;
   struct DmaBuffer **b2 = (struct DmaBuffer **)p2;

   if ((*b1)->buffHandle > (*b2)->buffHandle) return 1;
   if ((*b1)->buffHandle < (*b2)->buffHandle) return -1;
   return 0;
}

/**
 * dmaSearchComp - Compare a DMA buffer against a search key for searching.
 * @key: the search key, typically a DMA buffer handle.
 * @element: pointer to the DMA buffer to compare against the key.
 *
 * This function compares a given DMA buffer's handle against a search key
 * to facilitate efficient search operations, such as binary search, within
 * an array of DMA buffers.
 *
 * Return: 1 if the buffer handle is less than the search key, -1 if greater,
 * and 0 if equal.
 */
int32_t dmaSearchComp(const void *key, const void *element) {
   struct DmaBuffer **buff = (struct DmaBuffer **)element;
   dma_addr_t value = *((dma_addr_t *)key);

   if ((*buff)->buffHandle < value) return 1;
   if ((*buff)->buffHandle > value) return -1;
   return 0;
}

/**
 * dmaFindBufferList - Find a DMA buffer in a list by its handle.
 * @list: pointer to the DMA buffer list where the search is performed.
 * @handle: the DMA address (handle) of the buffer to find.
 *
 * This function searches for a DMA buffer within a given list by its handle.
 * The search is performed differently based on the organization of the list:
 * if the list is not sorted, it performs a linear search through all entries;
 * if the list is sorted, it uses a binary search for efficiency.
 *
 * Return: Pointer to the found DmaBuffer if successful, NULL if not found.
 */
struct DmaBuffer *dmaFindBufferList(struct DmaBufferList *list, dma_addr_t handle) {
   uint32_t x;
   uint32_t sl;
   uint32_t sli;

   // Handle unsorted list case: linear search required due to dynamic and unsorted entries
   if (list->sorted == NULL) {
      for (x = 0; x < list->count; x++) {
         sl = x / BUFFERS_PER_LIST;
         sli = x % BUFFERS_PER_LIST;
         if (list->indexed[sl][sli]->buffHandle == handle) {
            return list->indexed[sl][sli];
         }
      }
      // Buffer not found in unsorted list
      return NULL;

   // Handle sorted list case: binary search for efficiency
   } else {
      struct DmaBuffer **result = (struct DmaBuffer **)
         bsearch(&handle, list->sorted, list->count, sizeof(struct DmaBuffer *), dmaSearchComp);

      if (result == NULL) {
         // Buffer not found in sorted list
         return NULL;
      } else {
         return *result;
      }
   }
}

/**
 * dmaFindBuffer - Find a buffer from either the TX or RX list
 * @dev: pointer to the DmaDevice structure
 * @handle: DMA address to find
 *
 * This function searches for a DMA buffer that matches the given DMA address
 * in either the transmission (TX) or reception (RX) buffer lists of the device.
 *
 * Return: pointer to the DmaBuffer if found, NULL otherwise.
 */
struct DmaBuffer *dmaFindBuffer(struct DmaDevice *dev, dma_addr_t handle) {
   struct DmaBuffer *buff;

   if ((buff = dmaFindBufferList(&(dev->txBuffers), handle)) != NULL) return buff;
   if ((buff = dmaFindBufferList(&(dev->rxBuffers), handle)) != NULL) return buff;
   return NULL;
}

/**
 * dmaGetBufferList - Get a buffer using index in the passed list
 * @list: pointer to the DmaBufferList structure
 * @index: index of the buffer within the list
 *
 * Retrieves a DMA buffer from a specified list based on the buffer's index.
 * The function calculates the segment list and segment list index to locate
 * the buffer within the list's multi-dimensional array structure.
 *
 * Return: pointer to the DmaBuffer if within range, NULL otherwise.
 */
struct DmaBuffer *dmaGetBufferList(struct DmaBufferList *list, uint32_t index) {
   uint32_t sl;
   uint32_t sli;

   if (index < list->baseIdx || index >= (list->baseIdx + list->count)) {
       return NULL;
   } else {
      sl  = (index - list->baseIdx) / BUFFERS_PER_LIST;
      sli = (index - list->baseIdx) % BUFFERS_PER_LIST;
      return list->indexed[sl][sli];
   }
}

/**
 * dmaGetBuffer - Get a buffer using index, in either the TX or RX list
 * @dev: pointer to the DmaDevice structure
 * @index: index of the buffer
 *
 * Retrieves a DMA buffer by its index from either the transmission (TX) or
 * reception (RX) buffer lists of the device.
 *
 * Return: pointer to the DmaBuffer if found, NULL otherwise.
 */
struct DmaBuffer *dmaGetBuffer(struct DmaDevice *dev, uint32_t index) {
   struct DmaBuffer *buff;
   if ((buff = dmaGetBufferList(&(dev->txBuffers), index)) != NULL) return buff;
   if ((buff = dmaGetBufferList(&(dev->rxBuffers), index)) != NULL) return buff;
   return NULL;
}

/**
 * dmaRetBufferIrq - Conditionally return buffer to the transmit queue.
 * @dev: Pointer to the device structure.
 * @handle: DMA handle for the buffer to be returned.
 *
 * This function attempts to return a buffer to the device's transmit queue
 * based on the given DMA handle. If the buffer is found in the transmit list,
 * it is prepared for hardware interaction and re-queued. If not found in the
 * transmit list but found in the receive list, it is returned directly.
 * If the buffer cannot be found in either list, a warning is issued.
 *
 * Return: NULL if the buffer is re-queued or not found, or a pointer to the
 * buffer if it is found in the receive list.
 */
struct DmaBuffer *dmaRetBufferIrq(struct DmaDevice *dev, dma_addr_t handle) {
   struct DmaBuffer *buff;

   // Attempt to return buffer to transmit queue if found
   if ((buff = dmaFindBufferList(&(dev->txBuffers), handle)) != NULL) {
      dmaBufferFromHw(buff);   // Prepare buffer for hardware interaction
      dmaQueuePushIrq(&(dev->tq), buff); // Re-queue the buffer
      return NULL;

   // Attempt to return rx buffer if found in receive list
   } else if ((buff = dmaFindBufferList(&(dev->rxBuffers), handle)) != NULL) {
      return buff;

   // Log warning if buffer is not found in either list
   } else {
      dev_warn(dev->device, "dmaRetBufferIrq: Failed to locate descriptor %.8x.\n", (uint32_t)handle);
      return NULL;
   }
}

/**
 * dmaRetBufferIdx - Conditionally return buffer to transmit buffer
 * @dev: pointer to the DMA device structure
 * @index: index of the DMA buffer to retrieve
 *
 * This function attempts to return a buffer to the transmit queue based on
 * its index. If the buffer is found in the transmit list, it is returned to
 * the queue. If not found in the transmit list but found in the receive list,
 * a pointer to the buffer is returned. If the buffer is not found in either
 * list, a warning is issued, and NULL is returned. The intention is to manage
 * buffer allocation dynamically during DMA operations.
 *
 * Return: NULL if the buffer is returned to the transmit queue or if it cannot
 *         be found. Otherwise, returns a pointer to the found buffer.
 */
struct DmaBuffer *dmaRetBufferIdx(struct DmaDevice *dev, uint32_t index) {
   struct DmaBuffer *buff;

   // Attempt to return buffer to the transmit queue
   if ((buff = dmaGetBufferList(&(dev->txBuffers), index)) != NULL) {
      dmaBufferFromHw(buff);
      dmaQueuePush(&(dev->tq), buff);
      return NULL;

   // Attempt to retrieve and return the buffer from the receive queue
   } else if ((buff = dmaGetBufferList(&(dev->rxBuffers), index)) != NULL) {
      return buff;

   // Log warning if the buffer cannot be found in either queue
   } else {
      dev_warn(dev->device, "dmaRetBufferIdx: Failed to locate descriptor %i.\n", index);
      return NULL;
   }
}

/**
 * dmaRetBufferIdxIrq - Conditionally return buffer to transmit buffer.
 * @dev: pointer to the DmaDevice structure
 * @index: index of the DMA buffer in the buffer list
 *
 * This function attempts to return a buffer to the transmit queue if it is
 * found in the transmit list based on the provided index. If the buffer is
 * not found in the transmit list, it tries to find it in the receive list.
 * If the buffer is still not found, it logs a warning message. This function
 * is typically called within an IRQ context to manage buffer handling.
 *
 * Return: NULL if the buffer is successfully returned to the transmit queue
 *         or if it is not found. Otherwise, a pointer to the DmaBuffer if
 *         it is found in the receive list.
 */
struct DmaBuffer *dmaRetBufferIdxIrq(struct DmaDevice *dev, uint32_t index) {
   struct DmaBuffer *buff;

   // Attempt to return buffer to transmit queue if found
   if ((buff = dmaGetBufferList(&(dev->txBuffers), index)) != NULL) {
      dmaBufferFromHw(buff);
      dmaQueuePushIrq(&(dev->tq), buff);
      return NULL;

   // Attempt to return buffer from receive queue if found
   } else if ((buff = dmaGetBufferList(&(dev->rxBuffers), index)) != NULL) {
      return buff;

   // Log warning if buffer is not found in either list
   } else {
      dev_warn(dev->device, "dmaRetBufferIdxIrq: Failed to locate descriptor %i.\n", index);
      return NULL;
   }
}

/**
 * dmaRxBuffer - Push buffer to descriptor's receive queue.
 * @desc: pointer to the DmaDesc structure
 * @buff: pointer to the DmaBuffer to be pushed
 *
 * This function pushes a buffer to the descriptor's receive queue. It is
 * intended to be called outside of IRQ context. Before pushing, it ensures
 * the buffer is ready for software processing.
 */
void dmaRxBuffer(struct DmaDesc *desc, struct DmaBuffer *buff) {
   dmaBufferFromHw(buff);
   dmaQueuePush(&(desc->q), buff);
   if (desc->async_queue)
      kill_fasync(&desc->async_queue, SIGIO, POLL_IN);
}

/**
 * dmaRxBufferIrq - Push buffer to descriptor's receive queue from IRQ context.
 * @desc: pointer to the DmaDesc structure
 * @buff: pointer to the DmaBuffer to be pushed
 *
 * Similar to dmaRxBuffer, but specifically designed to be called from an IRQ
 * handler. This ensures that the operation is safe to perform from interrupt
 * context, making use of IRQ-safe queue operations.
 */
void dmaRxBufferIrq(struct DmaDesc *desc, struct DmaBuffer *buff) {
   dmaBufferFromHw(buff);
   dmaQueuePushIrq(&(desc->q), buff);
   if (desc->async_queue)
      kill_fasync(&desc->async_queue, SIGIO, POLL_IN);
}

/**
 * dmaSortBuffers - Sort a list of DMA buffers
 * @list: pointer to the DMA buffer list to be sorted
 *
 * This function sorts a list of DMA buffers based on a comparison function.
 * The sorting is necessary to ensure buffers are processed in the correct order.
 */
void dmaSortBuffers(struct DmaBufferList *list) {
   if (list->count > 0)
      sort(list->sorted, list->count, sizeof(struct DmaBuffer *), dmaSortComp, NULL);//NOLINT
}

/**
 * dmaBufferToHw - Prepare and pass a DMA buffer to hardware
 * @buff: pointer to the DMA buffer to be passed to hardware
 *
 * Prepares a DMA buffer for hardware access by synchronizing it for device use.
 * This function is called when a buffer is about to be passed to the hardware,
 * typically for DMA operations. It handles stream mode buffers by performing
 * necessary DMA sync operations.
 *
 * Returns 0 on success, or -1 on error.
 */
int32_t dmaBufferToHw(struct DmaBuffer *buff) {
   // Check if buffer is in stream mode and sync
   if (buff->buffList->dev->cfgMode & BUFF_STREAM) {
      dma_sync_single_for_device(buff->buffList->dev->device,
                                 buff->buffHandle,
                                 buff->buffList->dev->cfgSize,
                                 buff->buffList->direction);
   }

   buff->inHw = 1;
   return 0;
}

/**
 * dmaBufferFromHw - Retrieve a DMA buffer from hardware
 * @buff: pointer to the DMA buffer being returned from hardware
 *
 * Marks a DMA buffer as no longer being in hardware use and performs necessary
 * synchronization for CPU access. This function is called when a buffer is
 * returned from the hardware, typically after DMA operations are completed.
 * It handles stream mode buffers by performing necessary DMA sync operations.
 */
void dmaBufferFromHw(struct DmaBuffer *buff) {
   buff->inHw = 0;

   // Check if buffer is in stream mode and sync
   if (buff->buffList->dev->cfgMode & BUFF_STREAM) {
      dma_sync_single_for_cpu(buff->buffList->dev->device,
                              buff->buffHandle,
                              buff->buffList->dev->cfgSize,
                              buff->buffList->direction);
   }
}

/**
 * dmaQueueInit - Initialize a DMA queue.
 * @queue: pointer to the DMA queue to initialize.
 * @count: number of elements in the DMA queue.
 *
 * This function initializes a DMA queue structure, allocating memory for
 * the queue and its sub-queues based on the specified count. It also
 * initializes synchronization primitives used in queue operations.
 *
 * Return: The number of elements initialized in the queue on success,
 *         0 on allocation failure.
 */
size_t dmaQueueInit(struct DmaQueue *queue, uint32_t count) {
   uint32_t x;

   // Set queue parameters
   queue->count = count + 1;
   queue->subCount = (queue->count / BUFFERS_PER_LIST) + 1;
   queue->read = 0;
   queue->write = 0;

   // Allocate memory for the queue pointers
   queue->queue = (struct DmaBuffer ***)kzalloc(queue->subCount * sizeof(struct DmaBuffer **), GFP_KERNEL);
   if (queue->queue == NULL) {
      goto cleanup_force_exit;
   }

   // Allocate memory for each sub-queue
   for (x = 0; x < queue->subCount; x++) {
      queue->queue[x] = (struct DmaBuffer **)kzalloc(BUFFERS_PER_LIST * sizeof(struct DmaBuffer *), GFP_KERNEL);
      if (queue->queue[x] == NULL) {
         goto cleanup_sub_queue;
      }
   }

   // Initialize synchronization primitives
   spin_lock_init(&(queue->lock));
   init_waitqueue_head(&(queue->wait));

   // Return the original count as the number of initialized elements
   return count;

cleanup_sub_queue:
   // Cleanup in case of allocation failure for sub-queues
   for (x = 0; x < queue->subCount; x++)
      if (queue->queue[x] != NULL)
         kfree(queue->queue[x]);

cleanup_force_exit:
   // Return 0 to indicate failure to initialize the queue
   return 0;
}

/**
 * dmaQueueFree - Frees all allocated memory within the DMA queue.
 * @queue: pointer to the DmaQueue structure to be freed.
 *
 * This function releases all memory resources associated with the DMA queue,
 * including the queue elements and the queue array itself. It resets the queue
 * count and sub-count to zero.
 */
void dmaQueueFree(struct DmaQueue *queue) {
   uint32_t x;

   queue->count = 0;
   for (x = 0; x < queue->subCount; x++)
      if (queue->queue[x] != NULL)
         kfree(queue->queue[x]);

   queue->subCount = 0;
   kfree(queue->queue);
}

/**
 * dmaQueueNotEmpty - Checks if the DMA queue is not empty.
 * @queue: pointer to the DmaQueue structure to be checked.
 *
 * Return: 0 if the queue is empty, 1 if it is not empty.
 *
 * This function determines whether the DMA queue has any pending elements by
 * comparing the read and write pointers. It is useful for deciding whether
 * data processing or retrieval operations are necessary.
 */
uint32_t dmaQueueNotEmpty(struct DmaQueue *queue) {
   if (queue->read == queue->write)
      return 0;
   else
      return 1;
}

/**
 * dmaQueuePush - Push a queue entry.
 * @queue: pointer to the DMA queue structure.
 * @entry: pointer to the DMA buffer entry to be added to the queue.
 *
 * This function adds a new entry to the DMA queue. It should be used
 * outside of interrupt handlers to ensure proper synchronization.
 * The function employs a circular queue mechanism to manage DMA buffer
 * entries efficiently. It takes care of locking and ensures thread safety.
 *
 * Return: 0 on success, indicating the entry was successfully added to the queue.
 *         1 on failure, indicating a buffer overflow or other issue preventing
 *         the entry from being added to the queue.
 */
uint32_t dmaQueuePush(struct DmaQueue *queue, struct DmaBuffer *entry) {
   unsigned long iflags;
   uint32_t      next;
   uint32_t      ret;

   // Protect the queue update with spinlock to ensure thread safety
   spin_lock_irqsave(&(queue->lock), iflags);

   // Calculate the next write position in a circular buffer fashion
   next = (queue->write + 1) % (queue->count);
   ret = 0;

   // Check for buffer overflow - this condition should ideally never occur
   if (next == queue->read) {
      ret = 1; // Indicate failure due to no space left in the queue
   } else {
      // Add the entry to the queue and update the write index
      queue->queue[queue->write / BUFFERS_PER_LIST][queue->write % BUFFERS_PER_LIST] = entry;
      queue->write = next;
      entry->inQ = 1; // Mark the buffer as queued
   }

   // Release the spinlock and restore interrupts
   spin_unlock_irqrestore(&(queue->lock), iflags);

   // Wake up any process waiting for queue space to become available
   wake_up_interruptible(&(queue->wait));

   return ret;
}

/**
 * dmaQueuePushIrq - Push a queue entry from an interrupt handler.
 * @queue: pointer to the DMA queue structure where the entry will be added.
 * @entry: pointer to the DMA buffer entry to be pushed into the queue.
 *
 * This function is designed to be called from within an interrupt handler
 * to add a DMA buffer entry to a specified queue. It handles synchronization
 * through spin locks and updates the queue's write pointer after adding the
 * entry. In the case of a buffer overflow, which should not occur with proper
 * queue management, the function returns an error.
 *
 * Return: 0 on success, indicating the entry was successfully added to the queue.
 *         1 on failure, indicating a buffer overflow occurred.
 */
uint32_t dmaQueuePushIrq(struct DmaQueue *queue, struct DmaBuffer *entry) {
   uint32_t next;
   uint32_t ret;

   spin_lock(&(queue->lock));

   next = (queue->write + 1) % (queue->count);
   ret = 0;

   // Check for buffer overflow, which indicates a queue management issue.
   if (next == queue->read) {
      ret = 1;
   } else {
      // Safely add the entry to the queue and mark it as in the queue.
      queue->queue[queue->write / BUFFERS_PER_LIST][queue->write % BUFFERS_PER_LIST] = entry;
      queue->write = next;
      entry->inQ = 1;
   }

   spin_unlock(&(queue->lock));

   // Wake up any process waiting on the queue.
   wake_up_interruptible(&(queue->wait));

   return ret;
}

/**
 * dmaQueuePushList - Enqueue a list of buffers to the DMA queue.
 *
 * @queue: Pointer to the DMA queue structure.
 * @buff: Array of pointers to DmaBuffer structures to be enqueued.
 * @cnt: Number of buffers in the @buff array.
 *
 * This function enqueues a list of buffers into the specified DMA queue.
 * It ensures atomic access to the queue using spin locks and checks for
 * buffer overflow conditions. The function updates the buffer queueing
 * status and wakes up any processes waiting for buffers to be enqueued.
 *
 * Return: Returns 0 on success, indicating that all buffers have been
 * successfully enqueued. Returns 1 if a buffer overflow condition is
 * detected, indicating failure to enqueue all buffers.
 */
uint32_t dmaQueuePushList(struct DmaQueue *queue, struct DmaBuffer **buff, size_t cnt) {
   unsigned long iflags;
   uint32_t      next;
   uint32_t      ret;
   size_t        x;

   // Acquire the queue spin lock and save the interrupt flags
   spin_lock_irqsave(&(queue->lock), iflags);
   ret = 0;

   for (x = 0; x < cnt; x++) {
      // Calculate the next write position in a circular buffer manner
      next = (queue->write + 1) % (queue->count);

      // Check for buffer overflow condition
      if (next == queue->read) {
         // Overflow detected, unable to enqueue more buffers
         ret = 1;
         break;
      } else {
         // Enqueue the buffer into the queue and mark it as in the queue
         queue->queue[queue->write / BUFFERS_PER_LIST][queue->write % BUFFERS_PER_LIST] = buff[x];
         queue->write = next;
         buff[x]->inQ = 1;
      }
   }

   // Release the spin lock and restore the interrupt flags
   spin_unlock_irqrestore(&(queue->lock), iflags);

   // Wake up any processes waiting for buffers to be enqueued
   wake_up_interruptible(&(queue->wait));

   return ret;
}

/**
 * dmaQueuePushListIrq - Enqueue a block of buffers into the DMA queue.
 * @queue: Pointer to the DMA queue where buffers will be enqueued.
 * @buff: Array of pointers to DmaBuffer structures to be enqueued.
 * @cnt: Number of buffers in the @buff array.
 *
 * This function enqueues a list of DMA buffers into the specified queue
 * using an IRQ-safe method, suitable for calling within IRQ handlers.
 * It ensures atomic access to the queue and updates buffer states accordingly.
 *
 * The function uses spin_lock to protect the queue operations, ensuring that
 * the enqueue operation is safe to call in an interrupt context or when
 * concurrency issues might arise.
 *
 * Return: 0 on success, indicating that all buffers were successfully enqueued.
 *         1 on failure, indicating a buffer overflow or inability to enqueue
 *         all buffers.
 */
uint32_t dmaQueuePushListIrq(struct DmaQueue *queue, struct DmaBuffer **buff, size_t cnt) {
   uint32_t next;
   uint32_t ret;
   size_t x;

   spin_lock(&(queue->lock));
   ret = 0;

   for (x = 0; x < cnt; x++) {
      next = (queue->write + 1) % (queue->count);

      // Check for buffer overflow - this condition should not normally occur.
      if (next == queue->read) {
         ret = 1; // Indicate failure due to overflow.
         break;
      } else {
         // Properly place buffer in queue and mark it as in queue.
         queue->queue[queue->write / BUFFERS_PER_LIST][queue->write % BUFFERS_PER_LIST] = buff[x];
         queue->write = next;
         buff[x]->inQ = 1; // Mark buffer as enqueued.
      }
   }
   spin_unlock(&(queue->lock));

   // Wake up any processes waiting for buffers to become available.
   wake_up_interruptible(&(queue->wait));

   return ret;
}

/**
 * dmaQueuePop - Pop a queue entry.
 * @queue: pointer to the DmaQueue from which to pop the entry.
 *
 * This function pops an entry from the specified DMA queue. It should be used
 * outside of interrupt handlers. The function ensures mutual exclusion via
 * spinlocks and updates the queue's read pointer accordingly.
 *
 * Context: Can sleep if called outside of interrupt context.
 *
 * Return: A pointer to a DmaBuffer if available; NULL if the queue is empty.
 */
struct DmaBuffer * dmaQueuePop  ( struct DmaQueue *queue ) {
   unsigned long      iflags;
   struct DmaBuffer * ret;

   spin_lock_irqsave(&(queue->lock),iflags);

   if ( queue->read == queue->write ) {
      ret = NULL;
   } else {

      ret = queue->queue[queue->read / BUFFERS_PER_LIST][queue->read % BUFFERS_PER_LIST];

      // Increment read pointer safely within queue bounds
      queue->read = (queue->read + 1) % (queue->count);

      // Mark the buffer as not in queue
      ret->inQ = 0;
   }
   spin_unlock_irqrestore(&(queue->lock),iflags);
   return ret;
}

/**
 * dmaQueuePopIrq - Pop a queue entry for DMA operations
 * @queue: Pointer to the DmaQueue from which to pop the entry
 *
 * This function is intended for use within an interrupt handler to pop
 * a queue entry from a DMA buffer queue. It safely removes and returns
 * the next available DmaBuffer from the queue, if any, ensuring
 * synchronization via spinlocks. If the queue is empty, NULL is returned.
 *
 * Context: Can be called in interrupt context.
 *
 * Return: Pointer to the popped DmaBuffer, or NULL if the queue is empty.
 */
struct DmaBuffer *dmaQueuePopIrq(struct DmaQueue *queue) {
   struct DmaBuffer *ret;

   spin_lock(&(queue->lock));

   if (queue->read == queue->write) {
      ret = NULL;
   } else {
      // Calculate the next buffer to pop based on the current read index
      ret = queue->queue[queue->read / BUFFERS_PER_LIST][queue->read % BUFFERS_PER_LIST];

      // Increment read pointer in a circular queue fashion
      queue->read = (queue->read + 1) % (queue->count);

      // Mark the buffer as not in queue
      ret->inQ = 0;
   }

   spin_unlock(&(queue->lock));
   return ret;
}

/**
 * dmaQueuePopList - Dequeue a list of DMA buffers from a queue.
 * @queue: Pointer to the DmaQueue from which to dequeue buffers.
 * @buff: Pointer to an array of pointers to DmaBuffer where dequeued buffers will be stored.
 * @cnt: The maximum number of buffers to dequeue.
 *
 * This function attempts to dequeue up to @cnt buffers from the specified @queue,
 * storing the pointers to the dequeued buffers in the @buff array. It operates
 * with interrupt disabling to ensure thread safety in an interrupt context.
 *
 * Context: Can sleep if called in a non-interrupt context.
 * Return: The number of buffers actually dequeued. May be less than @cnt if the queue
 *         doesn't have enough buffers. Returns 0 if the queue is empty or on error.
 */
ssize_t dmaQueuePopList(struct DmaQueue *queue, struct DmaBuffer **buff, size_t cnt) {
   unsigned long iflags;
   ssize_t ret;

   ret = 0;
   // Protect the queue manipulation with a spinlock to ensure atomic access
   spin_lock_irqsave(&(queue->lock), iflags);

   // Loop to dequeue buffers until the count is met or the queue is empty
   while ( (ret < cnt) && (queue->read != queue->write) ) {
      // Calculate the current buffer's position and retrieve it
      buff[ret] = queue->queue[queue->read / BUFFERS_PER_LIST][queue->read % BUFFERS_PER_LIST];

      // Increment the read pointer in a circular manner
      queue->read = (queue->read + 1) % (queue->count);

      // Mark the buffer as no longer in the queue
      buff[ret]->inQ = 0;
      ret++;
   }

   // Release the spinlock and restore the saved interrupt flags
   spin_unlock_irqrestore(&(queue->lock), iflags);

   return ret;
}

/**
 * dmaQueuePopListIrq - Retrieve a block of buffers from a queue within an IRQ handler context.
 *
 * @queue: Pointer to the DmaQueue from which buffers are to be popped.
 * @buff: Double pointer to DmaBuffer to store the addresses of the buffers retrieved.
 * @cnt: The count of buffers to retrieve from the queue.
 *
 * This function attempts to retrieve a specified number of DMA buffers from a queue,
 * ensuring thread safety by using spin locks. It is specifically designed to be called
 * within an IRQ handler, using the IRQ-safe spin_lock and spin_unlock methods to protect
 * the queue's integrity during concurrent access. The function updates the 'inQ' status
 * of each buffer to indicate it is no longer in the queue.
 *
 * Return: The number of buffers successfully popped from the queue. This can be less than
 * 'cnt' if there are not enough buffers in the queue at the time of the call.
 */
ssize_t dmaQueuePopListIrq(struct DmaQueue *queue, struct DmaBuffer **buff, size_t cnt) {
   ssize_t ret;

   ret = 0;
   // Lock the queue to ensure exclusive access
   spin_lock(&(queue->lock));

   // Attempt to pop 'cnt' buffers from the queue
   while ((ret < cnt) && (queue->read != queue->write)) {
      // Retrieve the next buffer from the queue
      buff[ret] = queue->queue[queue->read / BUFFERS_PER_LIST][queue->read % BUFFERS_PER_LIST];

      // Increment the read pointer in a circular manner
      queue->read = (queue->read + 1) % (queue->count);

      // Mark the buffer as no longer in the queue
      buff[ret]->inQ = 0;
      ret++;
   }

   // Unlock the queue after operation
   spin_unlock(&(queue->lock));
   return ret;
}

/**
 * dmaQueuePoll - Poll a DMA queue for readiness
 * @queue: pointer to the DmaQueue structure
 * @filp: file pointer associated with the queue
 * @wait: poll table to wait on
 *
 * This function adds the file pointer and wait queue to the poll table,
 * allowing the process to wait for events on the DMA queue.
 */
void dmaQueuePoll(struct DmaQueue *queue, struct file *filp, poll_table *wait) {
   poll_wait(filp, &(queue->wait), wait);
}

/**
 * dmaQueueWait - Wait for data in the DMA queue
 * @queue: pointer to the DmaQueue structure
 *
 * Waits for data to become available in the DMA queue. The wait is interruptible,
 * allowing signal handling to interrupt the wait if necessary. This function
 * ensures that a process sleeps until there is data to read in the queue, comparing
 * the read and write pointers of the queue to determine availability.
 */
void dmaQueueWait(struct DmaQueue *queue) {
   wait_event_interruptible(queue->wait, (queue->read != queue->write));
}
