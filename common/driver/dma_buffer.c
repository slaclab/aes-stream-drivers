/**
 *-----------------------------------------------------------------------------
 * Title      : General purpose DMA buffers.
 * ----------------------------------------------------------------------------
 * File       : dma_buffer.c
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
#include <dma_buffer.h>
#include <asm/io.h>
#include <linux/dma-mapping.h>
#include <linux/sort.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <dma_common.h>

// Create a list of buffer
// Return number of buffers created
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

   if ( count == 0 ) return(0);

   // Allocate first level pointers
   if ((list->indexed = (struct DmaBuffer ***) kmalloc(sizeof(struct DmaBuffer**) * list->subCount, GFP_KERNEL)) == NULL ) {
      dev_err(dev->device,"dmaAllocBuffers: Failed to allocate indexed list pointer. Count=%u.\n",list->subCount);
      goto cleanup_forced_exit;
   }

   // Allocate sub lists
   for (x=0; x < list->subCount; x++) {
      if ((list->indexed[x] = (struct DmaBuffer **) kmalloc((sizeof(struct DmaBuffer *) * BUFFERS_PER_LIST), GFP_KERNEL)) == NULL) {
         dev_err(dev->device,"dmaAllocBuffers: Failed to allocate sub list. Idx=%u.\n",x);
         goto cleanup_lists;
      }
   }

   // Sorted lists are not always available. Disable for streaming mode or when we have too many buffers for
   // a single sorted list
   if ( (list->subCount == 1) && ((list->dev->cfgMode & BUFF_STREAM) == 0) ) {
      list->sorted = (struct DmaBuffer **) kmalloc(sizeof(struct DmaBuffer**) * count, GFP_KERNEL);
   }

   // Allocate buffers
   for (x=0; x < count; x++) {
      sl  = x / BUFFERS_PER_LIST;
      sli = x % BUFFERS_PER_LIST;
      if ( (buff = (struct DmaBuffer *) kmalloc(sizeof(struct DmaBuffer), GFP_KERNEL)) == NULL) {
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
            dma_alloc_coherent(list->dev->device, list->dev->cfgSize, &(buff->buffHandle), GFP_DMA32 | GFP_KERNEL);
      }

      // Streaming buffer type, standard kernel memory
      else if ( list->dev->cfgMode & BUFF_STREAM ) {
         buff->buffAddr = kmalloc(list->dev->cfgSize, GFP_KERNEL);

         if (buff->buffAddr != NULL) {
            buff->buffHandle = dma_map_single(list->dev->device,buff->buffAddr,
                                             list->dev->cfgSize,direction);
    
            // Map error
            if ( dma_mapping_error(list->dev->device,buff->buffHandle) ) {
               buff->buffHandle = 0;
            }
         }
      }

      // ACP type with permament handle mapping, dma capable kernel memory
      else if ( list->dev->cfgMode & BUFF_ARM_ACP ) {
         buff->buffAddr = kmalloc(list->dev->cfgSize, GFP_DMA | GFP_KERNEL);
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

   return(list->count);

   /* Cleanup */
cleanup_buffers:

   for (x=0; x < list->count; x++) {
      sl  = x / BUFFERS_PER_LIST;
      sli = x % BUFFERS_PER_LIST;

      if ( list->indexed[sl][sli]->buffAddr != NULL ) {

         // Coherent buffer
         if ( list->dev->cfgMode & BUFF_COHERENT ) {
            dma_free_coherent(list->dev->device, list->dev->cfgSize,list->indexed[sl][sli]->buffAddr,list->indexed[sl][sli]->buffHandle);
         }

         // Streaming type
         if ( list->dev->cfgMode & BUFF_STREAM ) {
            dma_unmap_single(list->dev->device,list->indexed[sl][sli]->buffHandle,list->dev->cfgSize,list->direction);
         }

         // Streaming buffer type or ARM ACP
         if ( (list->dev->cfgMode & BUFF_STREAM) || (list->dev->cfgMode & BUFF_ARM_ACP) ) {
            kfree(list->indexed[sl][sli]->buffAddr);
         }
         
      }
      kfree(list->indexed[sl][sli]);
   }
   list->count = 0;

   if ( list->sorted  != NULL ) kfree(list->sorted);

cleanup_lists:
   for (x=0; x < list->subCount; x++) 
      if ( list->indexed[x] != NULL ) 
         kfree(list->indexed[x]);
   if ( list->indexed != NULL ) kfree(list->indexed);

// Return 0 as no buffers were allocated
cleanup_forced_exit:
   return 0;
}


// Free a list of buffers
void dmaFreeBuffers ( struct DmaBufferList *list ) {
   uint32_t x;
   uint32_t sl;
   uint32_t sli;

   for (x=0; x < list->count; x++) {
      sl  = x / BUFFERS_PER_LIST;
      sli = x % BUFFERS_PER_LIST;

      if ( list->indexed[sl][sli]->buffAddr != NULL ) {

         // Coherent buffer
         if ( list->dev->cfgMode & BUFF_COHERENT ) {
            dma_free_coherent(list->dev->device, list->dev->cfgSize,list->indexed[sl][sli]->buffAddr,list->indexed[sl][sli]->buffHandle);
         }

         // Streaming type
         if ( list->dev->cfgMode & BUFF_STREAM ) {
            dma_unmap_single(list->dev->device,list->indexed[sl][sli]->buffHandle,list->dev->cfgSize,list->direction);
         }

         // Streaming buffer type or ARM ACP
         if ( (list->dev->cfgMode & BUFF_STREAM) || (list->dev->cfgMode & BUFF_ARM_ACP) ) {
            kfree(list->indexed[sl][sli]->buffAddr);
         }
      }
      kfree(list->indexed[sl][sli]);
   }
   for (x=0; x < list->subCount; x++) kfree(list->indexed[x]);
   if ( list->indexed != NULL ) kfree(list->indexed);
   if ( list->sorted  != NULL ) kfree(list->sorted);
}


// Generic Binary search
void *bsearch(const void *key, const void *base, size_t num, size_t size,
         int (*cmp)(const void *key, const void *elt)) {

   int start = 0, end = num - 1, mid, result;
   if (num == 0) return NULL;

   while (start <= end) {
      mid = (start + end) / 2;
      result = cmp(key, base + mid * size);
      if (result < 0)
         end = mid - 1;
      else if (result > 0)
         start = mid + 1;
      else
         return (void *)base + mid * size;
   }
   return NULL;
}


// Buffer comparison for sort
// Return comparison result, 1 if greater, -1 if less, 0 if equal
int32_t dmaSortComp (const void *p1, const void *p2) {
   struct DmaBuffer ** b1 = (struct DmaBuffer **)p1;
   struct DmaBuffer ** b2 = (struct DmaBuffer **)p2;

   if ( (*b1)->buffHandle > (*b2)->buffHandle ) return 1;
   if ( (*b1)->buffHandle < (*b2)->buffHandle ) return -1;
   return(0);
}


// Buffer comparison for search
// Return comparison result, 1 if greater, -1 if less, 0 if equal
int32_t dmaSearchComp (const void *key, const void *element) {
   struct DmaBuffer ** buff = (struct DmaBuffer **)element;

   dma_addr_t value = *((dma_addr_t *)key);

   if ( (*buff)->buffHandle < value ) return 1;
   if ( (*buff)->buffHandle > value ) return -1;
   return(0);
}

// Find a buffer, return index, or -1 on error
struct DmaBuffer * dmaFindBufferList ( struct DmaBufferList *list, dma_addr_t handle ) {
   uint32_t x;
   uint32_t sl;
   uint32_t sli;

   // Stream buffers have to be found with a loop because entries are dynamic and unsorted
   // We can only search if there is a single sub list
   if ( list->sorted == NULL ) {
      for ( x=0; x < list->count; x++ ) {
         sl  = x / BUFFERS_PER_LIST;
         sli = x % BUFFERS_PER_LIST;
         if ( list->indexed[sl][sli]->buffHandle == handle ) return(list->indexed[sl][sli]);
      }

      // Not found
      return(NULL);
   }

   // Sorted list search is more efficiant
   else {
      struct DmaBuffer ** result = (struct DmaBuffer **) 
         bsearch(&handle, list->sorted, list->count, sizeof(struct DmaBuffer *), dmaSearchComp);

      if ( result == NULL ) return(NULL);
      else return((*result));
   }
}

// Find a buffer from either list
struct DmaBuffer * dmaFindBuffer ( struct DmaDevice *dev, dma_addr_t handle ) {
   struct DmaBuffer *buff;

   if ( (buff = dmaFindBufferList (&(dev->txBuffers),handle)) != NULL) return(buff);
   if ( (buff = dmaFindBufferList (&(dev->rxBuffers),handle)) != NULL) return(buff);
   return(NULL);
}

// Get a buffer using index, in passed list
struct DmaBuffer * dmaGetBufferList ( struct DmaBufferList *list, uint32_t index ) {
   uint32_t sl;
   uint32_t sli;

   if ( index < list->baseIdx || index >= (list->baseIdx + list->count) ) return(NULL);
   else {
      sl  = (index - list->baseIdx) / BUFFERS_PER_LIST;
      sli = (index - list->baseIdx) % BUFFERS_PER_LIST;
      return(list->indexed[sl][sli]);
   }
}

// Get a buffer using index, in either list
struct DmaBuffer * dmaGetBuffer ( struct DmaDevice *dev, uint32_t index ) {
   struct DmaBuffer *buff;
   if ( ( buff = dmaGetBufferList(&(dev->txBuffers),index)) != NULL ) return(buff);
   if ( ( buff = dmaGetBufferList(&(dev->rxBuffers),index)) != NULL ) return(buff);
   return(NULL);
}

// Conditionally return buffer to transmit buffer. If buffer is not found in 
// transmit list return a pointer to the buffer. Passed value is the dma handle.
struct DmaBuffer * dmaRetBufferIrq ( struct DmaDevice *dev, dma_addr_t handle ) {
   struct DmaBuffer *buff;

   // Return buffer to transmit queue if it is found
   if ( (buff = dmaFindBufferList (&(dev->txBuffers),handle)) != NULL) {
      dmaBufferFromHw(buff);
      dmaQueuePushIrq(&(dev->tq),buff);
      return(NULL);
   }

   // Return rx buffer
   else if ( (buff = dmaFindBufferList (&(dev->rxBuffers),handle)) != NULL) {
      return(buff);
   }

   // Buffer is not found
   else {
      dev_warn(dev->device,"dmaRetBufferIrq: Failed to locate descriptor %.8x.\n",(uint32_t)handle);
      return(NULL);
   }
}

// Conditionally return buffer to transmit buffer. If buffer is not found in 
// transmit list return a pointer to the buffer. Passed value is the dma handle.
struct DmaBuffer * dmaRetBufferIdx ( struct DmaDevice *dev, uint32_t index ) {
   struct DmaBuffer *buff;

   // Return buffer to transmit queue if it is found
   if ( (buff = dmaGetBufferList (&(dev->txBuffers),index)) != NULL) {
      dmaBufferFromHw(buff);
      dmaQueuePush(&(dev->tq),buff);
      return(NULL);
   }

   // Return rx buffer
   else if ( (buff = dmaGetBufferList (&(dev->rxBuffers),index)) != NULL) {
      return(buff);
   }

   // Buffer is not found
   else {
      dev_warn(dev->device,"dmaRetBufferIdxIrq: Failed to locate descriptor %i.\n",index);
      return(NULL);
   }
}

// Conditionally return buffer to transmit buffer. If buffer is not found in 
// transmit list return a pointer to the buffer. Passed value is the dma handle.
struct DmaBuffer * dmaRetBufferIdxIrq ( struct DmaDevice *dev, uint32_t index ) {
   struct DmaBuffer *buff;

   // Return buffer to transmit queue if it is found
   if ( (buff = dmaGetBufferList (&(dev->txBuffers),index)) != NULL) {
      dmaBufferFromHw(buff);
      dmaQueuePushIrq(&(dev->tq),buff);
      return(NULL);
   }

   // Return rx buffer
   else if ( (buff = dmaGetBufferList (&(dev->rxBuffers),index)) != NULL) {
      return(buff);
   }

   // Buffer is not found
   else {
      dev_warn(dev->device,"dmaRetBufferIdxIrq: Failed to locate descriptor %i.\n",index);
      return(NULL);
   }
}

// Push buffer to descriptor receive queue
void dmaRxBuffer ( struct DmaDesc *desc, struct DmaBuffer *buff ) {
   dmaBufferFromHw(buff);
   dmaQueuePush(&(desc->q),buff);
   if (desc->async_queue) kill_fasync(&desc->async_queue, SIGIO, POLL_IN);
}

// Push buffer to descriptor receive queue
// Called inside IRQ routine
void dmaRxBufferIrq ( struct DmaDesc *desc, struct DmaBuffer *buff ) {
   dmaBufferFromHw(buff);
   dmaQueuePushIrq(&(desc->q),buff);
   if (desc->async_queue) kill_fasync(&desc->async_queue, SIGIO, POLL_IN);
}

// Sort a buffer list
void dmaSortBuffers ( struct DmaBufferList *list ) {
   if ( list->count > 0 ) 
      sort(list->sorted,list->count,sizeof(struct DmaBuffer *),dmaSortComp,NULL);
}

// Buffer being passed to hardware
// Return -1 on error
int32_t dmaBufferToHw ( struct DmaBuffer *buff) {

   // Buffer is stream mode, sync
   if ( buff->buffList->dev->cfgMode & BUFF_STREAM ) {
      dma_sync_single_for_device(buff->buffList->dev->device, 
                                 buff->buffHandle, 
                                 buff->buffList->dev->cfgSize,
                                 buff->buffList->direction);
   }

   buff->inHw = 1;
   return(0);
}

// Buffer being returned from hardware
void dmaBufferFromHw ( struct DmaBuffer *buff ) {
   buff->inHw = 0;

   // Buffer is stream mode, sync
   if ( buff->buffList->dev->cfgMode & BUFF_STREAM ) {
      dma_sync_single_for_cpu(buff->buffList->dev->device, 
                              buff->buffHandle, 
                              buff->buffList->dev->cfgSize,
                              buff->buffList->direction);
   }
}

// Init queue
// Return number initialized
size_t dmaQueueInit ( struct DmaQueue *queue, uint32_t count ) {
   uint32_t x;

   queue->count    = count+1;
   queue->subCount = (queue->count / BUFFERS_PER_LIST) + 1;
   queue->read     = 0;
   queue->write    = 0;

   queue->queue = (struct DmaBuffer ***)kmalloc(queue->subCount * sizeof(struct DmaBuffer **),GFP_KERNEL);
   if (queue->queue == NULL) {
      goto cleanup_force_exit;
   }

   for(x=0; x < queue->subCount; x++) {
      queue->queue[x] = (struct DmaBuffer **)kmalloc(BUFFERS_PER_LIST * sizeof(struct DmaBuffer *),GFP_KERNEL);
      if (queue->queue[x] == NULL) {
         goto cleanup_sub_queue;
      }
   }
   
   spin_lock_init(&(queue->lock));
   init_waitqueue_head(&(queue->wait));
   return(count);

   /* cleanup */
cleanup_sub_queue:
   for (x=0; x < queue->subCount; x++) 
      if (queue->queue[x] != NULL)
         kfree(queue->queue[x]);

cleanup_force_exit:
   return 0;

}

// Free queue
void dmaQueueFree ( struct DmaQueue *queue ) {
   uint32_t x;

   queue->count = 0;
   for (x=0; x < queue->subCount; x++) 
      if (queue->queue[x] != NULL)
         kfree(queue->queue[x]);

   queue->subCount = 0;
   kfree(queue->queue);
}

// Dma queue is not empty
// Return 0 if empty, 1 if not empty
uint32_t dmaQueueNotEmpty ( struct DmaQueue *queue ) {
   if ( queue->read == queue->write ) return(0);
   else return(1);
}

// Push a queue entry
// Use this routine outside of interrupt handler
// Return 1 if fail, 0 if success
uint32_t dmaQueuePush  ( struct DmaQueue *queue, struct DmaBuffer *entry ) {
   unsigned long iflags;
   uint32_t      next;
   uint32_t      ret;

   spin_lock_irqsave(&(queue->lock),iflags);

   next = (queue->write+1) % (queue->count);
   ret = 0;

   // Buffer overflow, should not occur
   if ( next == queue->read ) ret = 1;
   else {
      queue->queue[queue->write / BUFFERS_PER_LIST][queue->write % BUFFERS_PER_LIST] = entry;
      queue->write = next;
      entry->inQ = 1;
   }
   spin_unlock_irqrestore(&(queue->lock),iflags);
   wake_up_interruptible(&(queue->wait));

   return(ret);
}


// Push a queue entry
// Use this routine inside of interrupt handler
// Return 1 if fail, 0 if success
uint32_t dmaQueuePushIrq ( struct DmaQueue *queue, struct DmaBuffer *entry ) {
   uint32_t      next;
   uint32_t      ret;

   spin_lock(&(queue->lock));

   next = (queue->write+1) % (queue->count);
   ret = 0;

   // Buffer overflow, should not occur
   if ( next == queue->read ) ret = 1;
   else {
      queue->queue[queue->write / BUFFERS_PER_LIST][queue->write % BUFFERS_PER_LIST] = entry;
      queue->write = next;
      entry->inQ = 1;
   }
   spin_unlock(&(queue->lock));
   wake_up_interruptible(&(queue->wait));

   return(ret);
}


// Return a block of buffers from queue
// Return 1 if fail, 0 if success
uint32_t dmaQueuePushList  ( struct DmaQueue *queue, struct DmaBuffer **buff, size_t cnt) {
   unsigned long iflags;
   uint32_t      next;
   uint32_t      ret;
   size_t        x;

   spin_lock_irqsave(&(queue->lock),iflags);
   ret = 0;

   for (x=0; x < cnt; x++) {

      next = (queue->write+1) % (queue->count);

      // Buffer overflow, should not occur
      if ( next == queue->read ) {
         ret = 1;
         break;
      }
      else {
         queue->queue[queue->write / BUFFERS_PER_LIST][queue->write % BUFFERS_PER_LIST] = buff[x];
         queue->write = next;
         buff[x]->inQ = 1;
      }
   }

   spin_unlock_irqrestore(&(queue->lock),iflags);
   wake_up_interruptible(&(queue->wait));

   return(ret);
}


// Return a block of buffers from queue
// Return 1 if fail, 0 if success
// Use IRQ method inside of IRQ handler
uint32_t dmaQueuePushListIrq ( struct DmaQueue *queue, struct DmaBuffer **buff, size_t cnt ) {
   uint32_t      next;
   uint32_t      ret;
   size_t        x;

   spin_lock(&(queue->lock));
   ret = 0;

   for (x=0; x < cnt; x++) {

      next = (queue->write+1) % (queue->count);

      // Buffer overflow, should not occur
      if ( next == queue->read ) {
         ret = 1;
         break;
      }
      else {
         queue->queue[queue->write / BUFFERS_PER_LIST][queue->write % BUFFERS_PER_LIST] = buff[x];
         queue->write = next;
         buff[x]->inQ = 1;
      }
   }
   spin_unlock(&(queue->lock));
   wake_up_interruptible(&(queue->wait));

   return(ret);
}


// Pop a queue entry
// Use this routine outside of interrupt handler
// Return a queue entry, NULL if nothing available
struct DmaBuffer * dmaQueuePop  ( struct DmaQueue *queue ) {
   unsigned long      iflags;
   struct DmaBuffer * ret;

   spin_lock_irqsave(&(queue->lock),iflags);

   if ( queue->read == queue->write ) ret = NULL;
   else {

      ret = queue->queue[queue->read / BUFFERS_PER_LIST][queue->read % BUFFERS_PER_LIST];

      // Increment read pointer
      queue->read = (queue->read + 1) % (queue->count);

      ret->inQ = 0;
   }
   spin_unlock_irqrestore(&(queue->lock),iflags);
   return(ret);
}


// Pop a queue entry
// Use this routine inside of interrupt handler
// Return a queue entry, NULL if nothing available
struct DmaBuffer * dmaQueuePopIrq ( struct DmaQueue *queue ) {
   struct DmaBuffer * ret;

   spin_lock(&(queue->lock));

   if ( queue->read == queue->write ) ret = NULL;
   else {

      ret = queue->queue[queue->read / BUFFERS_PER_LIST][queue->read % BUFFERS_PER_LIST];

      // Increment read pointer
      queue->read = (queue->read + 1) % (queue->count);

      ret->inQ = 0;
   }
   spin_unlock(&(queue->lock));
   return(ret);
}


// Get a block of buffers from queue
ssize_t dmaQueuePopList ( struct DmaQueue *queue, struct DmaBuffer**buff, size_t cnt ) {
   unsigned long iflags;
   ssize_t ret;

   ret = 0;
   spin_lock_irqsave(&(queue->lock),iflags);

   while ( (ret < cnt) && (queue->read != queue->write) ) {
      buff[ret] = queue->queue[queue->read / BUFFERS_PER_LIST][queue->read % BUFFERS_PER_LIST];

      // Increment read pointer
      queue->read = (queue->read + 1) % (queue->count);

      buff[ret]->inQ = 0;
      ret++;
   }

   spin_unlock_irqrestore(&(queue->lock),iflags);
   return(ret);
}


// Get a block of buffers from queue
// Use IRQ method inside of IRQ handler
ssize_t dmaQueuePopListIrq ( struct DmaQueue *queue, struct DmaBuffer**buff, size_t cnt ) {
   ssize_t ret;

   ret = 0;
   spin_lock(&(queue->lock));

   while ( (ret < cnt) && (queue->read != queue->write) ) {
      buff[ret] = queue->queue[queue->read / BUFFERS_PER_LIST][queue->read % BUFFERS_PER_LIST];

      // Increment read pointer
      queue->read = (queue->read + 1) % (queue->count);

      buff[ret]->inQ = 0;
      ret++;
   }

   spin_unlock(&(queue->lock));
   return(ret);
}


// Poll queue
void dmaQueuePoll ( struct DmaQueue *queue, struct file *filp, poll_table *wait ) {
   poll_wait(filp,&(queue->wait),wait);
}


// Wait on queue
void dmaQueueWait ( struct DmaQueue *queue ){
   wait_event_interruptible(queue->wait,(queue->read != queue->write));
}

