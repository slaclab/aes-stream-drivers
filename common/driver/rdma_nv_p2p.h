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

#ifndef __RDMA_NV_P2P_H__
#define __RDMA_NV_P2P_H__

#include <dma_common.h>
#include <dma_buffer.h>
#include <rdma_common.h>
#include <linux/interrupt.h>
#ifdef HAVE_NV_P2P
#include <nv-p2p.h>
#endif


#ifdef HAVE_NV_P2P
/**
 * struct GpuBuffer - Represents a single GPU buffer
 * @write: Write flag indicating the buffer's usage
 * @address: Physical address of the buffer in memory
 * @size: Size of the buffer in bytes
 * @pageTable: Pointer to the NVIDIA-specific page table
 * @dmaMapping: Pointer to the DMA mapping structure for this buffer
 * @dev: Pointer to the DMA device; needed by unpin callback.
 *
 * This structure defines a single buffer's properties, including its
 * memory address, size, and associated NVIDIA page table and DMA mapping.
 */
struct GpuBuffer {
   uint32_t write;
   uint32_t size;
   uint64_t address;
   nvidia_p2p_page_table_t *pageTable;
   struct nvidia_p2p_dma_mapping *dmaMapping;
   struct DmaDevice* dev;
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
 * @disabled: Bool indicating we cleared FPGA state (a hack for V4)
 * @bufferDataCount: Index of next free entry in bufferData
 * @bufferData: Slab of memory for per-buffer private data. Total size is MaxReadBuffs + MaxWriteBuffs
 * @writeBuffers: GpuBuffers structure for write operations
 * @readBuffers: GpuBuffers structure for read operations
 *
 * This structure is designed to encapsulate all relevant data for
 * GPU operations, including pointers to read and write buffers.
 */
struct NvP2PData {
   struct GpuBuffers writeBuffers;
   struct GpuBuffers readBuffers;
};

// Function prototypes
struct NvP2PData* NvP2P_Init(struct DmaDevice *dev, uint32_t offset);
int32_t NvP2P_Ioctl(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);
int32_t NvP2P_AddNvidia(struct DmaDevice *dev, uint64_t arg);
int32_t NvP2P_RemNvidia(struct DmaDevice *dev, uint64_t arg);
void NvP2P_FreeNvidia(void * data);
void NvP2P_Shutdown(struct DmaDevice* dev);

#endif // HAVE_NV_P2P

#endif  // __RDMA_NV_P2P_H__
