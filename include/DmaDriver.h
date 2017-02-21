/**
 *-----------------------------------------------------------------------------
 * Title      : DMA Driver, Common Header
 * ----------------------------------------------------------------------------
 * File       : DmaDriver.h
 * Created    : 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * Defintions and inline functions for interacting drivers.
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
#ifndef __DMA_DRIVER_H__
#define __DMA_DRIVER_H__

#ifdef DMA_IN_KERNEL
#include <linux/types.h>
#else
#include <stdint.h>
#endif

// Error values
#define DMA_ERR_FIFO 0x01
#define DMA_ERR_LEN  0x02
#define DMA_ERR_MAX  0x04
#define DMA_ERR_BUS  0x08

// Commands
#define DMA_Get_Buff_Count 0x1001
#define DMA_Get_Buff_Size  0x1002
#define DMA_Set_Debug      0x1003
#define DMA_Set_Mask       0x1004
#define DMA_Ret_Index      0x1005
#define DMA_Get_Index      0x1006
#define DMA_Read_Ready     0x1007
#define DMA_Set_Mask64     0x1008

// TX Structure
// Size = 0 for return index
struct DmaWriteData {
   uint64_t  data;
   uint32_t  dest;
   uint32_t  flags;
   uint32_t  index;
   uint32_t  size;
   uint32_t  is32;
   uint32_t  pad;
};

// RX Structure
// Data = 0 for read index
struct DmaReadData {
   uint64_t   data;
   uint32_t   dest;
   uint32_t   flags;
   uint32_t   index;
   uint32_t   error;
   uint32_t   size;
   uint32_t   is32;
};

// Everything below is hidden during kernel module compile
#ifndef DMA_IN_KERNEL
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <sys/fcntl.h>

// Write Frame
static inline ssize_t dmaWrite(int32_t fd, const void * buf, size_t size, uint32_t flags, uint32_t dest) {
   struct DmaWriteData w;

   memset(&w,0,sizeof(struct DmaWriteData));
   w.dest    = dest;
   w.flags   = flags;
   w.size    = size;
   w.is32    = (sizeof(void *)==4);
   w.data    = (uint64_t)buf;

   return(write(fd,&w,sizeof(struct DmaWriteData)));
}

// Write Frame, memory mapped
static inline ssize_t dmaWriteIndex(int32_t fd, uint32_t index, size_t size, uint32_t flags, uint32_t dest) {
   struct DmaWriteData w;

   memset(&w,0,sizeof(struct DmaWriteData));
   w.dest    = dest;
   w.flags   = flags;
   w.size    = size;
   w.is32    = (sizeof(void *)==4);
   w.index   = index;

   return(write(fd,&w,sizeof(struct DmaWriteData)));
}

// Receive Frame
static inline ssize_t dmaRead(int32_t fd, void * buf, size_t maxSize, uint32_t * flags, uint32_t *error, uint32_t * dest) {
   struct DmaReadData r;
   ssize_t ret;

   memset(&r,0,sizeof(struct DmaReadData));
   r.size = maxSize;
   r.is32 = (sizeof(void *)==4);
   r.data = (uint64_t)buf;

   ret = read(fd,&r,sizeof(struct DmaReadData));

   if ( dest  != NULL ) *dest  = r.dest;
   if ( flags != NULL ) *flags = r.flags;
   if ( error != NULL ) *error = r.error;

   return(ret);
}

// Receive Frame, access memory mapped buffer
// Returns receive size
static inline ssize_t dmaReadIndex(int32_t fd, uint32_t * index, uint32_t * flags, uint32_t *error, uint32_t * dest) {
   struct DmaReadData r;
   size_t ret;

   memset(&r,0,sizeof(struct DmaReadData));

   ret = read(fd,&r,sizeof(struct DmaReadData));

   if ( dest  != NULL ) *dest  = r.dest;
   if ( flags != NULL ) *flags = r.flags;
   if ( index != NULL ) *index = r.index;
   if ( error != NULL ) *error = r.error;

   return(ret);
}

// Post Index
static inline ssize_t dmaRetIndex(int32_t fd, uint32_t index) {
   return(ioctl(fd,DMA_Ret_Index,index));
}

// Get write buffer index
static inline uint32_t dmaGetIndex(int32_t fd) {
   return(ioctl(fd,DMA_Get_Index,0));
}

// Get read ready status
static inline ssize_t dmaReadReady(int32_t fd) {
   return(ioctl(fd,DMA_Read_Ready,0));
}

// Return user space mapping to dma buffers
static inline void ** dmaMapDma(int32_t fd, uint32_t *count, uint32_t *size) {
   void *   temp;
   void **  ret;
   uint32_t bCount;
   uint32_t bSize;
   uint32_t x;

   bSize  = ioctl(fd,DMA_Get_Buff_Size,0);
   bCount = ioctl(fd,DMA_Get_Buff_Count,0);

   if ( count != NULL ) *count = bCount;
   if ( size  != NULL ) *size  = bSize;

   if ( (ret = (void **)malloc(sizeof(void *) * bCount)) == 0 ) return(NULL);

   for (x=0; x < bCount; x++) {

      if ( (temp = mmap (0, bSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (bSize*x))) == MAP_FAILED) {
         free(ret);
         return(NULL);
      }

      ret[x] = temp;
   }

   return(ret);
}

// Free space mapping to dma buffers
static inline ssize_t dmaUnMapDma(int32_t fd, void ** buffer) {
   uint32_t  bCount;
   uint32_t  bSize;
   uint32_t  x;;

   bCount = ioctl(fd,DMA_Get_Buff_Count,0);
   bSize  = ioctl(fd,DMA_Get_Buff_Size,0);

   // I don't think this is correct.....
   for (x=0; x < bCount; x++) munmap (buffer, bSize);

   free(buffer);
   return(0);
}

// Set debug
static inline ssize_t dmaSetDebug(int32_t fd, uint32_t level) {
   return(ioctl(fd,DMA_Set_Debug,level));
}

// set lane/vc rx mask, one bit per vc
static inline ssize_t dmaSetMask(int32_t fd, uint32_t mask) {
   return(ioctl(fd,DMA_Set_Mask,mask));
}

// set lane/vc rx mask, 64-bit version, one bit per vc
static inline ssize_t dmaSetMask64(int32_t fd, uint64_t mask) {
   uint64_t lMask = mask;
   return(ioctl(fd,DMA_Set_Mask64,&lMask));
}

// Assign interrupt handler
static inline void dmaAssignHandler (int32_t fd, void (*handler)(int32_t)) {
   struct sigaction act;
   int32_t oflags;

   act.sa_handler = handler;
   sigemptyset(&act.sa_mask);
   act.sa_flags = 0;

   sigaction(SIGIO, &act, NULL);
   fcntl(fd, F_SETOWN, getpid());
   oflags = fcntl(fd, F_GETFL);
   fcntl(fd, F_SETFL, oflags | FASYNC);
}

#endif
#endif

