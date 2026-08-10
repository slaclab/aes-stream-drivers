/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Common code for the RDMA interfaces.
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
#ifndef __RDMA_COMMON_H__
#define __RDMA_COMMON_H__

#include <dma_common.h>
#include <linux/types.h>

/**
 * GPU_BOUND_SHIFT - Shift for GPU address boundary
 */
#define GPU_BOUND_SHIFT   16

/**
 * GPU_BOUND_SIZE - Size of GPU address boundary
 */
#define GPU_BOUND_SIZE    ((u64)1 << GPU_BOUND_SHIFT)

/**
 * GPU_BOUND_OFFSET - Offset for GPU address boundary calculation
 */
#define GPU_BOUND_OFFSET  (GPU_BOUND_SIZE - 1)

/**
 * GPU_BOUND_MASK - Mask for aligning addresses to GPU boundary
 */
#define GPU_BOUND_MASK    (~GPU_BOUND_OFFSET)

/**
 * MAX_GPU_BUFFERS - Maximum number of GPU buffers allowed
 */
#define MAX_GPU_BUFFERS   1024

struct RdmaData {
   uint8_t * base;
   uint32_t offset;
   int32_t version;
   uint32_t maxBuffers;
   uint32_t disabled;
   atomic64_t pid;
   struct mutex lock;

   uint32_t writeBufferCount;
   uint32_t readBufferCount;

   void* dmaBufData;
   void* nvP2PData;
};

int32_t Rdma_Init(struct DmaDevice *dev, uint32_t offset);
int Rdma_Ioctl(struct DmaDevice* dev, uint32_t cmd, uint64_t arg);
int32_t Rdma_SetWriteEn(struct DmaDevice *dev, uint64_t arg);
void Rdma_Show(struct seq_file *s, struct DmaDevice *dev);
int32_t Rdma_EnableTx(struct DmaDevice *dev, uint64_t enable);
int32_t Rdma_EnableRx(struct DmaDevice *dev, uint64_t enable);
void Rdma_ClearBufferRegs(struct DmaDevice* dev);
void Rdma_Shutdown(struct DmaDevice* dev);

#endif // __RDMA_COMMON_H__
