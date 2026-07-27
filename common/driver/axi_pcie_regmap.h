/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description: Abstraction of the axi-pcie-core register mapping
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
#ifndef _AXI_PCIE_REGMAP_H_
#define _AXI_PCIE_REGMAP_H_

#include <linux/types.h>
#include <dma_common.h>

/** 
 * Register block indices.
 */
typedef enum reg_block_index {
   REG_AXIS_GEN2 = 0,
   REG_PHY,
   REG_AXI_VERSION,
   REG_AXI_SYSMON,
   REG_PROM,
   REG_USER,
   REG_GPU_ASYNC,

   REG_COUNT,
} reg_block_index_t;

#define INVALID_REG_OFFSET 0xFFFFFFFF

/**
 * Maximum supported register map version. Make sure to bump this when you update
 * the axi-pcie-core main register map.
 */
#define MAX_SUPPORTED_REG_VERSION 1

int AxiRegMap_Init(struct DmaDevice* dev, __iomem void* axiVersion);
uint32_t AxiRegMap_GetOffset(struct DmaDevice* dev, reg_block_index_t block);
uint32_t AxiRegMap_GetSize(struct DmaDevice* dev, reg_block_index_t block);

#endif  // _AXI_PCIE_REGMAP_H_
