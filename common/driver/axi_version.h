/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    Provides access and utility functions for the AXI version register space.
 *    This header file defines the structure and prototypes for managing and
 *    interacting with AXI version data.
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

#ifndef __AXI__VERSION_H__
#define __AXI__VERSION_H__

#include <linux/types.h>
#include <dma_common.h>
#include <AxiVersion.h>

/**
 * struct AxiVersion_Reg - AXI Version register space
 * @firmwareVersion: Firmware version number
 * @scratchPad: General purpose scratch pad register
 * @upTimeCount: Counter for uptime in clock cycles
 * @spareA: Reserved space
 * @haltReload: Halt and reload signal control
 * @fpgaReload: FPGA reload signal control
 * @fpgaReloadAddr: FPGA reload address
 * @userReset: User reset control
 * @spareB: Reserved space
 * @fdValue: Front door value for secure access
 * @spareC: Reserved space
 * @userValues: User definable register space
 * @deviceId: Device identification register
 * @spareD: Reserved space
 * @gitHash: GIT hash value for the firmware build
 * @spareE: Reserved space
 * @dnaValue: Device DNA value
 * @spareF: Reserved space
 * @buildString: Firmware build string
 */
struct AxiVersion_Reg {
   uint32_t firmwareVersion;   // 0x0000
   uint32_t scratchPad;        // 0x0004
   uint32_t upTimeCount;       // 0x0008

   uint32_t spareA[61];        // 0x000C - 0x00FC

   uint32_t haltReload;        // 0x0100
   uint32_t fpgaReload;        // 0x0104
   uint32_t fpgaReloadAddr;    // 0x0108
   uint32_t userReset;         // 0x010C

   uint32_t spareB[124];       // 0x0110 - 0x02fC

   uint32_t fdValue[2];        // 0x0300 - 0x0304

   uint32_t spareC[62];        // 0x0308 - 0x03FC

   uint32_t userValues[64];    // 0x0400 - 0x04FC
   uint32_t deviceId;          // 0x0500

   uint32_t spareD[63];        // 0x0504 - 0x05FC

   uint32_t gitHash[40];       // 0x0600 - 0x06A0

   uint32_t spareE[24];        // 0x06A4 - 0x06FC

   uint32_t dnaValue[4];       // 0x0700 - 0x070C

   uint32_t spareF[60];        // 0x0710 - 0x07FC

   uint32_t buildString[64];   // 0x0800 - 0x08FC
};

// Function prototypes
int32_t AxiVersion_Get(struct DmaDevice *dev, void *base, uint64_t arg);
void AxiVersion_Read(struct DmaDevice *dev, void *base, struct AxiVersion *aVer);
void AxiVersion_Show(struct seq_file *s, struct DmaDevice *dev, struct AxiVersion *aVer);
void AxiVersion_SetUserReset(void *base, bool state);

#endif  // __AXI__VERSION_H__
