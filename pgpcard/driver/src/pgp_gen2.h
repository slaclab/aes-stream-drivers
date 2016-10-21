/**
 *-----------------------------------------------------------------------------
 * Title      : PGP Card Gen1 & Gen2 Functions
 * ----------------------------------------------------------------------------
 * File       : pgp_gen2.h
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * Access functions for Gen1 & Gen2 PGP Cards
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
#ifndef __PGP_GEN2_H__
#define __PGP_GEN2_H__

#include "dma_include.h"
#include <dma_common.h>
#include <dma_buffer.h>
#include <linux/interrupt.h>

// Address Map, offset from base
struct PgpCardG2Reg {
   uint32_t version;     // 0x000
   uint32_t scratch;     // 0x004
   uint32_t irq;         // 0x008
   uint32_t control;     // 0x00C
   uint32_t l0Data;      // 0x010
   uint32_t l1Data;      // 0x014
   uint32_t l2Data;      // 0x018
   uint32_t l3Data;      // 0x01C

   uint32_t spare0[8];   // 0x020 - 0x03C

   uint32_t pgp0Stat;    // 0x040
   uint32_t pgp1Stat;    // 0x044
   uint32_t pgp2Stat;    // 0x048
   uint32_t pgp3Stat;    // 0x04C

   uint32_t spare1[12];  // 0x050 - 0x07C

   uint32_t pciStat0;    // 0x080
   uint32_t pciStat1;    // 0x084
   uint32_t pciStat2;    // 0x088
   uint32_t pciStat3;    // 0x08C

   uint32_t spare2[220]; // 0x090 - 0x3FC

   uint32_t rxFree;      // 0x400
   uint32_t rxMaxFrame;  // 0x404
   uint32_t rxStatus;    // 0x408
   uint32_t rxCount;     // 0x40C

   uint32_t spare3[4];   // 0x410 - 0x41C

   uint32_t rxRead0;     // 0x420
   uint32_t rxRead1;     // 0x424

   uint32_t spare4[246]; // 0x428 - 0x7FC

   uint32_t txL0Wr0;     // 0x800
   uint32_t txL0Wr1;     // 0x804
   uint32_t txL1Wr0;     // 0x808
   uint32_t txL1Wr1;     // 0x80C
   uint32_t txL2Wr0;     // 0x810
   uint32_t txL2Wr1;     // 0x814
   uint32_t txL3Wr0;     // 0x818
   uint32_t txL3Wr1;     // 0x81C
   uint32_t txStatus;    // 0x820
   uint32_t txRead;      // 0x824
   uint32_t txCount;     // 0x828

   uint32_t spare5[245]; // 0x82C - 0xBFC

   uint32_t promData;    // 0xC00
   uint32_t promAddr;    // 0xC04
   uint32_t promRead;    // 0xC08
};

// Set functions for gen2 card
extern struct hardware_functions PgpCardG2_functions;

// Interrupt handler
irqreturn_t PgpCardG2_Irq(int irq, void *dev_id);

// Init card in top level Probe
void PgpCardG2_Init(struct DmaDevice *dev);

// Clear card in top level Remove
void PgpCardG2_Clear(struct DmaDevice *dev);

// Return receive buffer to card
void PgpCardG2_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer *buff);

// Send a buffer
int32_t PgpCardG2_SendBuffer(struct DmaDevice *dev, struct DmaBuffer *buff);

// Execute command
int32_t PgpCardG2_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);

// Add data to proc dump
void PgpCardG2_SeqShow(struct seq_file *s, struct DmaDevice *dev);

///////////////////////////////////
// Local helper functions
///////////////////////////////////

// Get PCI Status
void PgpCardG2_GetPci(struct DmaDevice *dev, struct PciStatus * status);

// Get Lane Status
void PgpCardG2_GetStatus(struct DmaDevice *dev, struct PgpStatus *status, uint8_t lane);

#endif

