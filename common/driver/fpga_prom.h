/**
 *-----------------------------------------------------------------------------
 * Title      : Common FPGA Prom Functions
 * ----------------------------------------------------------------------------
 * File       : fpga_prom.h
 * Created    : 2017-03-16
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
#ifndef __FPGA__PROM_H__
#define __FPGA__PROM_H__
#include <linux/types.h>
#include <dma_common.h>

struct FpgaProm_Reg {
   uint32_t promData; 
   uint32_t promAddr; 
   uint32_t promRead; 
};

// Prom Read 
int32_t FpgaProm_Write(struct DmaDevice *dev, void * base, uint64_t arg);

// Prom write 
int32_t FpgaProm_Read(struct DmaDevice *dev, void * base, uint64_t arg);

#endif

