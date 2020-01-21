/**
 *-----------------------------------------------------------------------------
 * Title      : GPU Async Functions
 * ----------------------------------------------------------------------------
 * File       : gpu_async.h
 * ----------------------------------------------------------------------------
 * Description:
 * Helper functions for GPU Async interface
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
#ifndef __GPU_ASYNC_H__
#define __GPU_ASYNC_H__

#include <dma_common.h>
#include <dma_buffer.h>
#include <linux/interrupt.h>
#include <nv-p2p.h>

// NVIDIA Stuff
#define GPU_BOUND_SHIFT   16
#define GPU_BOUND_SIZE    ((u64)1 << GPU_BOUND_SHIFT)
#define GPU_BOUND_OFFSET  (GPU_BOUND_SIZE-1)
#define GPU_BOUND_MASK    (~GPU_BOUND_OFFSET)
#define MAX_GPU_BUFFERS   16

struct GpuBuffer {
   uint32_t write;
   uint64_t address;
   uint32_t size;
   nvidia_p2p_page_table_t *pageTable;
   struct nvidia_p2p_dma_mapping *dmaMapping;
};

struct GpuBuffers {
   struct GpuBuffer list[MAX_GPU_BUFFERS];
   uint32_t count;
};

struct GpuData {
   struct AxisG2GpuBuffers gpuWriteBuffers;
   struct AxisG2GpuBuffers gpuReadBuffers;
};

// Execute command
int32_t Gpu_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);

// Add NVIDIA Memory
int32_t Gpu_AddNvidia(struct DmaDevice *dev, uint64_t arg);

// Rem NVIDIA Memory
int32_t Gpu_RemNvidia(struct DmaDevice *dev, uint64_t arg);

// NVIDIA Callback
void Gpu_FreeNvidia(void * data);

#endif

