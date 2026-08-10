/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Provides a generic RDMA interface.
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

#ifndef __RDMA_DMA_BUF_H__
#define __RDMA_DMA_BUF_H__

#include <dma_common.h>

void* DmaBuf_Init(struct DmaDevice* dev, uint32_t offset);
void DmaBuf_Shutdown(struct DmaDevice* dev);
int DmaBuf_Ioctl(struct DmaDevice* dev, uint32_t cmd, uint64_t arg0);

#endif // __RDMA_DMA_BUF_H__
