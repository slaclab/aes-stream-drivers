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
#include "dma_buffer.h"
#include <asm/io.h>
#include <linux/dma-mapping.h>
#include <linux/sort.h>
#include <linux/sched.h>
#include <linux/slab.h>

// Create a list of buffer
// Return number of buffers created
size_t dmaAllocBuffers ( struct device *dev, struct DmaBufferList *list, uint32_t size, 
                         uint32_t count, uint32_t baseIdx, uint8_t mode, enum dma_data_direction direction) {
   uint32_t x;

   if ( count == 0 ) return(0);
   list->size      = size;
   list->count     = 0;
   list->buffMode  = mode;
   list->direction = direction;
   list->dev       = dev;
   list->baseIdx   = baseIdx;

   // Allocate space for tracking arrays
   list->indexed = (struct DmaBuffer **) kmalloc((sizeof(struct DmaBuffer *) * count), GFP_KERNEL);
   list->sorted  = (struct DmaBuffer **) kmalloc((sizeof(struct DmaBuffer *) * count), GFP_KERNEL);

   // Allocate buffers
   for (x=0; x < count; x++) {
      list->indexed[x] = (struct DmaBuffer *) kmalloc(sizeof(struct DmaBuffer), GFP_KERNEL);

      // Init record
      memset(list->indexed[x],0,sizeof(struct DmaBuffer));

      // Setup pointer back to list
      list->indexed[x]->buffList = (void *)list;

      // Coherent buffer, map dma coherent buffers
      if ( list->buffMode == BUFF_COHERENT ) {
         list->indexed[x]->buffAddr = 
            dma_alloc_coherent(list->dev, list->size, &(list->indexed[x]->buffHandle),GFP_KERNEL);
      }

      // Streaming buffer type, standard kernel memory
      else if ( list->buffMode == BUFF_STREAM ) {
         list->indexed[x]->buffAddr = kmalloc(size, GFP_KERNEL);
      }

      // ACP type with permament handle mapping, dma capable kernel memory
      else if ( list->buffMode == BUFF_ARM_ACP ) {
         list->indexed[x]->buffAddr = kmalloc(size, GFP_DMA | GFP_KERNEL);
         if (list->indexed[x]->buffAddr != NULL)
            list->indexed[x]->buffHandle = virt_to_phys(list->indexed[x]->buffAddr);
         if ( list->indexed[x]->buffHandle == 0 ) {
            kfree(list->indexed[x]);
            break;
         }
      } 

      // Alloc failed
      if ( list->indexed[x]->buffAddr == NULL ) {
         kfree(list->indexed[x]);
         break; 
      }

      // Set index
      list->indexed[x]->index = x + list->baseIdx;

      // Populate entry in sorted list for later sort
      list->sorted[x] = list->indexed[x];
      list->count++;
   }

   // Sort the buffers
   if ( list->count > 0 ) sort(list->sorted,list->count,sizeof(struct DmaBuffer *),dmaSortComp,NULL);

   return(list->count);
}


// Free a list of buffers
void dmaFreeBuffers ( struct DmaBufferList *list ) {
   uint32_t x;

   for (x=0; x < list->count; x++) {
      if ( list->indexed[x]->buffAddr != NULL ) {

         // Coherent buffer
         if ( list->buffMode == BUFF_COHERENT ) {
            dma_free_coherent(list->dev, list->size,list->indexed[x]->buffAddr,list->indexed[x]->buffHandle);
         }

         // Streaming type still mapped to hardware
         if ( list->buffMode == BUFF_STREAM && list->indexed[x]->buffHandle != 0 ) {
            dma_unmap_single(list->dev,list->indexed[x]->buffHandle,list->size,list->direction);
         }

         // Streaming buffer type or ARM ACP
         if ( list->buffMode == BUFF_STREAM || list->buffMode == BUFF_ARM_ACP ) {
            kfree(list->indexed[x]->buffAddr);
         }
      }
      kfree(list->indexed[x]);
   }
   if ( list->count > 0 ) {
      kfree(list->indexed);
      kfree(list->sorted);
   }
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
struct DmaBuffer * dmaFindBuffer ( struct DmaBufferList *list, dma_addr_t handle ) {
   uint32_t x;

   // Stream buffers have to be found with a loop because entries are dynamic and unsorted
   if ( list->buffMode == BUFF_STREAM ) {
      for ( x=0; x < list->count; x++ ) {
         if ( list->indexed[x]->buffHandle == handle ) return(list->indexed[x]);
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


// Get a buffer using index
struct DmaBuffer * dmaGetBuffer ( struct DmaBufferList *list, uint32_t index ) {
   if ( index < list->baseIdx || index >= (list->baseIdx + list->count) ) return(NULL);
   else return(list->indexed[index - list->baseIdx]);
}


// Sort a buffer list
void dmaSortBuffers ( struct DmaBufferList *list ) {
   if ( list->count > 0 ) 
      sort(list->sorted,list->count,sizeof(struct DmaBuffer *),dmaSortComp,NULL);
}

// Buffer being passed to hardware
int32_t dmaBufferToHw ( struct DmaBuffer *buff) {

   struct DmaBufferList *list = (struct DmaBufferList *)buff->buffList;

   // Buffer is stream mode and does not have a handle
   if ( list->buffMode == BUFF_STREAM && buff->buffHandle == 0 ) {

      // Attempt to map
      buff->buffHandle = dma_map_single(list->dev,buff->buffAddr,list->size,list->direction);

      // Map error
      if ( dma_mapping_error(list->dev,buff->buffHandle) ) return(1);
   }
   buff->inHw = 1;
   return(0);
}

// Buffer being returned from hardware
void dmaBufferFromHw ( struct DmaBuffer *buff ) {

   struct DmaBufferList *list = (struct DmaBufferList *)buff->buffList;

   buff->inHw = 0;

   // Unmap buffer if in streaming mode
   if ( list->buffMode == BUFF_STREAM && buff->buffHandle != 0 ) {
      dma_unmap_single(list->dev,buff->buffHandle,list->size,list->direction);
      buff->buffHandle = 0;
   }
}

// Init queue
// Return number initialized
size_t dmaQueueInit ( struct DmaQueue *queue, uint32_t count ) {
   queue->count = count+1;
   queue->read  = 0;
   queue->write = 0;
   queue->queue = (struct DmaBuffer **)kmalloc(queue->count * sizeof(struct DmaBuffer *),GFP_KERNEL);

   spin_lock_init(&(queue->lock));
   init_waitqueue_head(&(queue->wait));
   return(count);
}


// Free queue
void dmaQueueFree ( struct DmaQueue *queue ) {
   queue->count = 0;
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

   // Buffer overflow, should not occur
   if ( next == queue->read ) ret = 1;
   else {
      queue->queue[queue->write] = entry;
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

   // Buffer overflow, should not occur
   if ( next == queue->read ) ret = 1;
   else {
      queue->queue[queue->write] = entry;
      queue->write = next;
      entry->inQ = 1;
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

      ret = queue->queue[queue->read];

      // Increment read pointer
      queue->read = (queue->read + 1) % (queue->count);

      ret->inQ = 0;
   }
   spin_unlock_irqrestore(&(queue->lock),iflags);
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

