/**
 *-----------------------------------------------------------------------------
 * Title      : PGP Card Gen3 Functions
 * ----------------------------------------------------------------------------
 * File       : pgp_gen3.h
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-11
 * Last update: 2016-08-11
 * ----------------------------------------------------------------------------
 * Description:
 * Access functions for Gen3
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
#ifndef __PGP_GEN3_H__
#define __PGP_GEN3_H__

#include "dma_include.h"
#include <dma_common.h>
#include <dma_buffer.h>
#include <linux/interrupt.h>

// Address Map, offset from base
struct PgpCardG3Reg {

   //PciApp.vhd  
   uint32_t version;         // Software_Addr = 0x000,        Firmware_Addr(13 downto 2) = 0x000
   uint32_t serNumLower;     // Software_Addr = 0x004,        Firmware_Addr(13 downto 2) = 0x001
   uint32_t serNumUpper;     // Software_Addr = 0x008,        Firmware_Addr(13 downto 2) = 0x002
   uint32_t scratch;         // Software_Addr = 0x00C,        Firmware_Addr(13 downto 2) = 0x003
   uint32_t cardRstStat;     // Software_Addr = 0x010,        Firmware_Addr(13 downto 2) = 0x004
   uint32_t irq;             // Software_Addr = 0x014,        Firmware_Addr(13 downto 2) = 0x005 
   uint32_t pgpRate;         // Software_Addr = 0x018,        Firmware_Addr(13 downto 2) = 0x006
   uint32_t vciMode;         // Software_Addr = 0x01C,        Firmware_Addr(13 downto 2) = 0x007
   uint32_t pgpOpCode;       // Software_Addr = 0x020,        Firmware_Addr(13 downto 2) = 0x008
   uint32_t sysSpare0[2];    // Software_Addr = 0x028:0x024,  Firmware_Addr(13 downto 2) = 0x00A:0x009
   uint32_t pciStat[4];      // Software_Addr = 0x038:0x02C,  Firmware_Addr(13 downto 2) = 0x00E:0x00B
   uint32_t sysSpare1;       // Software_Addr = 0x03C,        Firmware_Addr(13 downto 2) = 0x00F 
   
   uint32_t evrCardStat[5];  // Software_Addr = 0x050:0x040,  Firmware_Addr(13 downto 2) = 0x012:0x010  
   uint32_t evrSpare0[11];   // Software_Addr = 0x07C:0x054,  Firmware_Addr(13 downto 2) = 0x01F:0x013
   
   uint32_t pgpCardStat[2];  // Software_Addr = 0x084:0x080,  Firmware_Addr(13 downto 2) = 0x021:0x020       
   uint32_t pgpSpare0[54];   // Software_Addr = 0x15C:0x088,  Firmware_Addr(13 downto 2) = 0x05F:0x022
   
   uint32_t syncCode[8];     // Software_Addr = 0x17C:0x160,  Firmware_Addr(13 downto 2) = 0x067:0x060       
   uint32_t runCode[8];      // Software_Addr = 0x19C:0x180,  Firmware_Addr(13 downto 2) = 0x067:0x060       
   uint32_t acceptCode[8];   // Software_Addr = 0x1BC:0x1A0,  Firmware_Addr(13 downto 2) = 0x06F:0x068         
      
   uint32_t runDelay[8];     // Software_Addr = 0x1DC:0x1C0,  Firmware_Addr(13 downto 2) = 0x077:0x070       
   uint32_t acceptDelay[8];  // Software_Addr = 0x1FC:0x1E0,  Firmware_Addr(13 downto 2) = 0x07F:0x078       

   uint32_t pgpLaneStat[8];  // Software_Addr = 0x21C:0x200,  Firmware_Addr(13 downto 2) = 0x087:0x080       
   uint32_t evrRunCnt[8];    // Software_Addr = 0x23C:0x220,
   uint32_t lutDropCnt[8];   // Software_Addr = 0x25C:0x240,
   uint32_t acceptCnt[8];    // Software_Addr = 0x27C:0x260,
   uint32_t pgpData[8];      // Software_Addr = 0x29C:0x280,
   uint32_t pgpSpare1[24];   // Software_Addr = 0x2FC:0x2A0,
   uint32_t BuildStamp[64];  // Software_Addr = 0x3FC:0x300,  Firmware_Addr(13 downto 2) = 0x0FF:0x0C0
   
   //PciRxDesc.vhd   
   uint32_t rxFree[8];       // Software_Addr = 0x41C:0x400,  Firmware_Addr(13 downto 2) = 0x107:0x100   
   uint32_t rxSpare0[24];    // Software_Addr = 0x47C:0x420,  Firmware_Addr(13 downto 2) = 0x11F:0x108
   uint32_t rxFreeStat[8];   // Software_Addr = 0x49C:0x480,  Firmware_Addr(13 downto 2) = 0x127:0x120      
   uint32_t rxSpare1[24];    // Software_Addr = 0x4FC:0x4A0,  Firmware_Addr(13 downto 2) = 0x13F:0x128
   uint32_t rxMaxFrame;      // Software_Addr = 0x500,        Firmware_Addr(13 downto 2) = 0x140 
   uint32_t rxCount;         // Software_Addr = 0x504,        Firmware_Addr(13 downto 2) = 0x141 
   uint32_t rxStatus;        // Software_Addr = 0x508,        Firmware_Addr(13 downto 2) = 0x142
   uint32_t rxRead[4];       // Software_Addr = 0x518:0x50C,  Firmware_Addr(13 downto 2) = 0x146:0x143      
   uint32_t rxSpare2[185];   // Software_Addr = 0x7FC:0x51C,  Firmware_Addr(13 downto 2) = 0x1FF:0x147
   
   //PciTxDesc.vhd
   uint32_t txWrA[8];        // Software_Addr = 0x81C:0x800,  Firmware_Addr(13 downto 2) = 0x207:0x200   
   uint32_t txFifoCnt[8];    // Software_Addr = 0x83C:0x820,  Firmware_Addr(13 downto 2) = 0x20F:0x208
   uint32_t txSpare0[16];    // Software_Addr = 0x87C:0x840,  Firmware_Addr(13 downto 2) = 0x21F:0x210
   uint32_t txWrB[8];        // Software_Addr = 0x89C:0x880,  Firmware_Addr(13 downto 2) = 0x227:0x220      
   uint32_t txSpare1[24];    // Software_Addr = 0x8FC:0x8A0,  Firmware_Addr(13 downto 2) = 0x23F:0x228   
   uint32_t txStat[2];       // Software_Addr = 0x904:0x900,  Firmware_Addr(13 downto 2) = 0x241:0x240      
   uint32_t txCount;         // Software_Addr = 0x908,        Firmware_Addr(13 downto 2) = 0x242  
   uint32_t txRead;          // Software_Addr = 0x90C,        Firmware_Addr(13 downto 2) = 0x243  
   uint32_t txSpare[188];    // Software_Addr = 0x910:0xBFC   

   uint32_t promData;        // Software_Addr = 0xC00
   uint32_t promAddr;        // Software_Addr = 0xC04
   uint32_t promRead;        // Software_Addr = 0xC08
};

// Set functions for gen3 card
extern struct hardware_functions PgpCardG3_functions;

// Interrupt handler
irqreturn_t PgpCardG3_Irq(int irq, void *dev_id);

// Init card in top level Probe
void PgpCardG3_Init(struct DmaDevice *dev);

// Clear card in top level Remove
void    PgpCardG3_Clear(struct DmaDevice *dev);

// Return receive buffer to card
void    PgpCardG3_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer *buff);

// Send a buffer
int32_t PgpCardG3_SendBuffer(struct DmaDevice *dev, struct DmaBuffer *buff);

// Execute command
int32_t PgpCardG3_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);

// Add data to proc dump
void PgpCardG3_SeqShow(struct seq_file *s, struct DmaDevice *dev);

///////////////////////////////////
// Local helper functions
///////////////////////////////////

// Get PCI Status
void PgpCardG3_GetPci(struct DmaDevice *dev, struct PciStatus * status);

// Get Lane Status
void PgpCardG3_GetStatus(struct DmaDevice *dev, struct PgpStatus *status, uint8_t lane);

// Get EVR Control
void PgpCardG3_GetEvrControl(struct DmaDevice *dev, struct PgpEvrControl *control, uint8_t lane);

// Set EVR Control
void PgpCardG3_SetEvrControl(struct DmaDevice *dev, struct PgpEvrControl *control, uint8_t lane);

// Get EVR Status
void PgpCardG3_GetEvrStatus(struct DmaDevice *dev, struct PgpEvrStatus *status, uint8_t lane);

#endif

