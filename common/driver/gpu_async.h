/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    This header file provides declarations for helper functions used to facilitate
 *    asynchronous GPU operations within the kernel space. It includes interfaces for
 *    initializing GPU tasks, managing data buffers, and handling asynchronous callbacks.
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

#ifndef __GPU_ASYNC_2_H__
#define __GPU_ASYNC_2_H__

#include <dma_common.h>
#include <dma_buffer.h>
#include <linux/interrupt.h>
#include <nv-p2p.h>
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
#define MAX_GPU_BUFFERS   16

/**
 * struct GpuBuffer - Represents a single GPU buffer
 * @write: Write flag indicating the buffer's usage
 * @address: Physical address of the buffer in memory
 * @size: Size of the buffer in bytes
 * @pageTable: Pointer to the NVIDIA-specific page table
 * @dmaMapping: Pointer to the DMA mapping structure for this buffer
 *
 * This structure defines a single buffer's properties, including its
 * memory address, size, and associated NVIDIA page table and DMA mapping.
 */
struct GpuBuffer {
   uint32_t write;
   uint64_t address;
   uint32_t size;
   nvidia_p2p_page_table_t *pageTable;
   struct nvidia_p2p_dma_mapping *dmaMapping;
};

/**
 * struct GpuBuffers - Container for multiple GPU buffers
 * @list: Array of GpuBuffer structures
 * @count: Number of buffers currently in use
 *
 * This structure acts as a container for managing multiple GpuBuffer
 * instances. It tracks the buffers in use and their count.
 */
struct GpuBuffers {
   struct GpuBuffer list[MAX_GPU_BUFFERS];
   uint32_t count;
};

/**
 * struct GpuData - High-level structure representing GPU-related data
 * @base: Base pointer to the GPU data in memory
 * @writeBuffers: GpuBuffers structure for write operations
 * @readBuffers: GpuBuffers structure for read operations
 *
 * This structure is designed to encapsulate all relevant data for
 * GPU operations, including pointers to read and write buffers.
 */
struct GpuData {
   uint8_t * base;
   struct GpuBuffers writeBuffers;
   struct GpuBuffers readBuffers;
};

// Function prototypes
void Gpu_Init(struct DmaDevice *dev, uint32_t offset);
int32_t Gpu_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);
int32_t Gpu_AddNvidia(struct DmaDevice *dev, uint64_t arg);
int32_t Gpu_RemNvidia(struct DmaDevice *dev, uint64_t arg);
void Gpu_FreeNvidia(void * data);

#endif // __GPU_ASYNC_2_H__
