/**
 *-----------------------------------------------------------------------------
 * Title      : PGP Card Driver, Shared Header
 * ----------------------------------------------------------------------------
 * File       : PgpDriver.h
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * Defintions and inline functions for interacting with PGP driver.
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
#ifndef __PGP_DRIVER_H__
#define __PGP_DRIVER_H__
#include <DmaDriver.h>

#ifdef DMA_IN_KERNEL
#include <linux/types.h>
#else
#include <stdint.h>
#endif

// Card Info
struct PgpInfo {
   uint64_t serial;
   uint32_t type;
   uint32_t version;
   uint32_t laneMask;
   uint32_t vcPerMask;
   uint32_t pgpRate;
   uint32_t promPrgEn;
   uint32_t evrSupport;
   uint32_t pad;
   char     buildStamp[256];
};


// PCI Info
struct PciStatus {
   uint32_t pciCommand;
   uint32_t pciStatus;
   uint32_t pciDCommand;
   uint32_t pciDStatus;
   uint32_t pciLCommand;
   uint32_t pciLStatus;
   uint32_t pciLinkState;
   uint32_t pciFunction;
   uint32_t pciDevice;
   uint32_t pciBus;
   uint32_t pciLanes;
   uint32_t pad;
};


// Lane status
struct PgpStatus {
   uint32_t lane;
   uint32_t loopBack;
   uint32_t locLinkReady;
   uint32_t remLinkReady;
   uint32_t rxReady;
   uint32_t txReady;
   uint32_t rxCount;
   uint32_t cellErrCnt;
   uint32_t linkDownCnt;
   uint32_t linkErrCnt;
   uint32_t fifoErr;
   uint32_t remData;
   uint32_t remBuffStatus;
   uint32_t pad;
};


// EVR Control, per lane
struct PgpEvrControl {
   uint32_t  lane;
   uint32_t  evrEnable;     // Global flag
   uint32_t  laneRunMask;   // 1 = Run trigger enable
   uint32_t  evrSyncEn;     // 1 = Start, 0 = Stop
   uint32_t  evrSyncSel;    // 0 = async, 1 = sync for start/stop
   uint32_t  headerMask;    // 1 = Enable header data checking, one bit per VC (4 bits)
   uint32_t  evrSyncWord;   // fiducial to transition start stop
   uint32_t  runCode;       // Run code
   uint32_t  runDelay;      // Run delay
   uint32_t  acceptCode;    // Accept code
   uint32_t  acceptDelay;   // Accept delay
   uint32_t  pad;
};


// EVR Status, per lane
struct PgpEvrStatus {
   uint32_t  lane;
   uint32_t  linkErrors;
   uint32_t  linkUp;
   uint32_t  runStatus;    // 1 = Running, 0 = Stopped
   uint32_t  evrSeconds;
   uint32_t  runCounter;
   uint32_t  acceptCounter;
   uint32_t  pad;
};


// Card Types
#define PGP_NONE     0x00
#define PGP_GEN1     0x01
#define PGP_GEN2     0x02
#define PGP_GEN2_VCI 0x12
#define PGP_GEN3     0x03
#define PGP_GEN3_VCI 0x13

// Error values
#define PGP_ERR_EOFE 0x10

// Commands
#define PGP_Read_Info      0x2001
#define PGP_Read_Pci       0x2002
#define PGP_Read_Status    0x2003
#define PGP_Set_Loop       0x2004
#define PGP_Count_Reset    0x2005
#define PGP_Send_OpCode    0x2006
#define PGP_Set_Data       0x2007
#define PGP_Write_Prom     0x2008
#define PGP_Read_Prom      0x2009
#define PGP_Set_Evr_Cntrl  0x3001
#define PGP_Get_Evr_Cntrl  0x3002
#define PGP_Get_Evr_Status 0x3003
#define PGP_Rst_Evr_Count  0x3004

// Prom Programming 
struct PgpPromData {
   uint32_t address;
   uint32_t cmd;
   uint32_t data;
   uint32_t pad;
};

// Everything below is hidden during kernel module compile
#ifndef DMA_IN_KERNEL
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <sys/fcntl.h>

// Write Frame
static inline ssize_t pgpWrite(int32_t fd, void * buf, size_t size, uint32_t lane, uint32_t vc, uint32_t cont=0) {
   uint32_t flags;
   uint32_t dest;

   flags = cont;
   dest  = cont;

   dest  = lane * 4;
   dest += vc;

   return(dmaWrite(fd, buf, size, flags, dest));
}

// Send Frame, memory mapped buffer
// Returns transmit size
static inline ssize_t pgpWriteIndex(int32_t fd, uint32_t index, size_t size, uint32_t lane, uint32_t vc, uint32_t cont) {
   uint32_t flags;
   uint32_t dest;

   flags = cont;
   dest  = cont;

   dest  = lane * 4;
   dest += vc;

   return(dmaWriteIndex(fd, index, size, flags, dest));
}

// Receive Frame
static inline ssize_t pgpRead(int32_t fd, void * buf, size_t maxSize, uint32_t * lane, uint32_t * vc, uint32_t * error, uint32_t * cont=NULL) {
   uint32_t flags;
   uint32_t dest;
   ssize_t  ret;

   ret = dmaRead(fd,buf,maxSize,&flags,error,&dest);

   if ( lane  != NULL ) *lane  = dest / 4;
   if ( vc    != NULL ) *vc    = dest % 4;
   if ( cont  != NULL ) *cont  = flags;
   return(ret);
}

// Receive Frame, access memory mapped buffer
// Returns receive size
static inline ssize_t pgpReadIndex(int32_t fd, uint32_t * index, uint32_t * lane, uint32_t * vc, uint32_t * error, uint32_t * cont=NULL) {
   uint32_t flags;
   uint32_t dest;
   ssize_t  ret;

   ret = dmaReadIndex(fd,index,&flags,error,&dest);

   if ( lane  != NULL ) *lane  = dest / 4;
   if ( vc    != NULL ) *vc    = dest % 4;
   if ( cont  != NULL ) *cont  = flags;
   return(ret);
}

// Read Card Info
static inline ssize_t pgpGetInfo(int32_t fd, struct PgpInfo * info) {
   return(ioctl(fd,PGP_Read_Info,info));
}

// Read PCI Status
static inline ssize_t pgpGetPci(int32_t fd, struct PciStatus * status) {
   return(ioctl(fd,PGP_Read_Pci,status));
}

// Read Lane Status
static inline ssize_t pgpGetStatus(int32_t fd, uint32_t lane, struct PgpStatus * status) {
   status->lane = lane;
   return(ioctl(fd,PGP_Read_Status,status));
}

// Set Loopback State For Lane
static inline ssize_t pgpSetLoop(int32_t fd, uint32_t lane, uint32_t state) {
   uint32_t temp;

   temp = lane & 0xFF;
   temp |= ((state << 8) & 0x100);

   return(ioctl(fd,PGP_Set_Loop,temp));
}

// Reset counters
static inline ssize_t pgpCountReset(int32_t fd) {
   return(ioctl(fd,PGP_Count_Reset,0));
}

// Set Sideband Data
static inline ssize_t pgpSetData(int32_t fd, uint32_t lane, uint32_t data) {
   uint32_t temp;

   temp = lane & 0xFF;
   temp |= ((data << 8) & 0xFF00);

   return(ioctl(fd,PGP_Set_Data,temp));
}

// Send OpCode
static inline ssize_t pgpSendOpCode(int32_t fd, uint32_t code) {
   return(ioctl(fd,PGP_Send_OpCode,code));
}

// Set EVR Control
static inline ssize_t pgpSetEvrControl(int32_t fd, uint32_t lane, struct PgpEvrControl * control) {
   control->lane = lane;
   return(ioctl(fd,PGP_Set_Evr_Cntrl,control));
}


// Get EVR Control
static inline ssize_t pgpGetEvrControl(int32_t fd, uint32_t lane, struct PgpEvrControl * control) {
   control->lane = lane;
   return(ioctl(fd,PGP_Get_Evr_Cntrl,control));
}


// Get EVR Status
static inline ssize_t pgpGetEvrStatus(int32_t fd, uint32_t lane, struct PgpEvrStatus * status) {
   status->lane = lane;
   return(ioctl(fd,PGP_Get_Evr_Status,status));
}

// Reset EVR Counters
static inline ssize_t pgpResetEvrCount(int32_t fd, uint32_t lane) {
   return(ioctl(fd,PGP_Rst_Evr_Count,lane));
}

// Write to PROM
static inline ssize_t pgpWriteProm(int32_t fd, uint32_t address, uint32_t cmd, uint32_t data) {
   struct PgpPromData prom;

   prom.address = address;
   prom.cmd     = cmd;
   prom.data    = data;
   return(ioctl(fd,PGP_Write_Prom,&prom));
}

// Read from PROM
static inline ssize_t pgpReadProm(int32_t fd, uint32_t address, uint32_t cmd, uint32_t *data) {
   struct PgpPromData prom;
   ssize_t res;

   prom.address = address;
   prom.cmd     = cmd;
   prom.data    = 0;
   res = ioctl(fd,PGP_Read_Prom,&prom);

   if ( data != NULL ) *data = prom.data;

   return(res);
}

#endif
#endif

