/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description: Linux hwmon interface for UltraScale devices implementing the
 *  SysMon IP block.
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
#ifndef AXI_HWMON_H_
#define AXI_HWMON_H_

#include "dma_common.h"

void AxiHwmon_Init(struct DmaDevice* dev, off_t axiVerOffset, off_t axiSysMonOffset);
void AxiHwmon_Remove(struct DmaDevice* dev);

#endif // AXI_HWMON_H_