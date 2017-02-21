/**
 *-----------------------------------------------------------------------------
 * Title      : AXIS DMA Driver, Shared Header
 * ----------------------------------------------------------------------------
 * File       : AxisDriver.h
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * Defintions and inline functions for interacting with AXIS driver.
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
#ifndef __ASIS_DRIVER_H__
#define __ASIS_DRIVER_H__
#include <DmaDriver.h>

#ifdef DMA_IN_KERNEL
#include <linux/types.h>
#else
#include <stdint.h>
#endif

// Commands
#define AXIS_Read_Ack 0x2001

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
static inline ssize_t axisWrite(int32_t fd, void * buf, size_t size, uint32_t fuser, uint32_t luser, uint32_t dest) {
   uint32_t flags;

   flags  = fuser & 0xFF;
   flags += (luser << 8) & 0xFF00;

   return(dmaWrite(fd, buf, size, flags, dest));
}

// Write Frame, memory mapped
static inline ssize_t axisWriteIndex(int32_t fd, uint32_t index, size_t size, uint32_t fuser, uint32_t luser, uint32_t dest) {
   uint32_t flags;

   flags  = fuser & 0xFF;
   flags += (luser << 8) & 0xFF00;

   return(dmaWriteIndex(fd, index, size, flags, dest));
}

// Receive Frame
static inline ssize_t axisRead(int32_t fd, void * buf, size_t maxSize, uint32_t * fuser, uint32_t * luser, uint32_t * dest, uint32_t * cont = NULL) {
   uint32_t flags;
   uint32_t error;
   ssize_t  ret;

   ret = dmaRead(fd, buf, maxSize, &flags, &error, dest);

   if ( fuser != NULL ) *fuser = flags & 0xFF;
   if ( luser != NULL ) *luser = (flags >> 8) & 0xFF;
   if ( cont  != NULL ) *cont  = (flags >> 16) & 0x1;
   return(error==0?ret:-error);
}

// Receive Frame, access memory mapped buffer
// Returns receive size
static inline ssize_t axisReadIndex(int32_t fd, uint32_t * index, uint32_t * fuser, uint32_t * luser, uint32_t * dest, uint32_t * cont = NULL) {
   uint32_t flags;
   uint32_t error;
   ssize_t  ret;

   ret = dmaReadIndex(fd, index, &flags, &error, dest);

   if ( fuser != NULL ) *fuser = flags & 0xFF;
   if ( luser != NULL ) *luser = (flags >> 8) & 0xFF;
   if ( cont  != NULL ) *cont  = (flags >> 16) & 0x1;
   return(error==0?ret:-error);
}

// Read ACK
static inline void axisReadAck (int32_t fd) {
   ioctl(fd,AXIS_Read_Ack,0);
}

#endif
#endif

