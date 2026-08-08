/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    This module provides an interface for managing GPU tasks asynchronously,
 *    facilitating non-blocking operations and efficient GPU utilization. It
 *    includes functions for initializing the GPU for async operations, queueing
 *    tasks, handling completion callbacks, and cleanup procedures.
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

#include <rdma_nv_p2p.h>
#include <rdma_common.h>
#include <GpuAsync.h>
#include <GpuAsyncRegs.h>

#include <linux/seq_file.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <nv-p2p.h>

/**
 * @brief Return a pointer to the nv-p2p private data for the device, or NULL
 */
static struct NvP2PData* NvP2P_GetPvt(struct DmaDevice* dev) {
   struct RdmaData* rd = dev->rdmaData;
   if (!rd)
      return NULL;
   return (struct NvP2PData*)rd->nvP2PData;
}

/**
 * NvP2P_Init - Initialize GPU with given offset
 * @dev: pointer to the DmaDevice structure
 * @offset: memory offset for GPU initialization
 *
 * This function allocates memory for GpuData structure, initializes
 * it, and associates it with the given DmaDevice. It sets up
 * the base address for GPU operations and initializes buffer counts.
 */
struct NvP2PData* NvP2P_Init(struct DmaDevice *dev, uint32_t offset) {
   (void) offset;

   struct NvP2PData* data = kzalloc(sizeof(struct NvP2PData), GFP_KERNEL);
   if (!data) {
      dev_err(dev->device, "NvP2P_Init: Failed to allocate NvP2PData struct\n");
      return NULL;
   }
   return data;
}

/**
 * NvP2P_Ioctl - Execute command on GPU.
 * @dev: Pointer to the DmaDevice structure.
 * @cmd: Command to execute.
 * @arg: Argument for the command.
 *
 * This function executes a specified command on the GPU.
 * It supports adding and removing NVIDIA Memory. If the command
 * is not recognized, it logs a warning and returns an error.
 *
 * Return: 0 on success, -1 on error.
 */
int32_t NvP2P_Ioctl(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   if (!NvP2P_GetPvt(dev)) {
      return -ENOTSUPP;
   }

   switch (cmd) {
      // Add NVIDIA Memory
      case GPU_Add_Nvidia_Memory:
         return NvP2P_AddNvidia(dev, arg);

      // Remove NVIDIA Memory
      case GPU_Rem_Nvidia_Memory:
         return NvP2P_RemNvidia(dev, arg);

      default:
         dev_warn(dev->device, "NvP2P_Ioctl: Invalid command=%u\n", cmd);
         return -1;
   }
}

/**
 * NvP2P_AddNvidia - Add NVIDIA GPU memory to the device
 * @dev: pointer to the DMA device structure
 * @arg: user space argument pointing to GpuNvidiaData structure
 *
 * This function adds NVIDIA GPU memory for DMA operations. It involves
 * copying data from user space, validating it, and setting up DMA mappings
 * through NVIDIA's Peer-to-Peer (P2P) API.
 *
 * Return: 0 on success, negative error code on failure.
 */
int32_t NvP2P_AddNvidia(struct DmaDevice *dev, uint64_t arg) {
   int32_t ret;
   uint32_t x;
   u64     virt_start, virt_offset, dma_address;
   size_t  pin_size;
   size_t  mapSize;
   uint32_t offset = 0;
   size_t  minSize = 0;

   struct RdmaData  * rdmaData;
   struct GpuBuffer * buffer;
   struct GpuNvidiaData dat;
   struct NvP2PData * data;

   rdmaData = dev->rdmaData;
   if (!rdmaData)
      return -EINVAL;

   data = NvP2P_GetPvt(dev);
   if (!data)
      return -EINVAL;

   // Copy data from user space
   if ((ret = copy_from_user(&dat, (void *)arg, sizeof(struct GpuNvidiaData)))) {
      dev_warn(dev->device, "NvP2P_AddNvidia: copy_from_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &dat);
      return -1;
   }

   if (!dat.size) {
      dev_warn(dev->device, "NvP2P_AddNvidia: error: Buffer has size of 0 bytes\n");
      return -EINVAL;
   }

   if ((dat.size & ~GPU_BOUND_MASK) != 0) {
      dev_warn(dev->device, "NvP2P_AddNvidia: error: memory size (%u) is not a multiple of GPU page size (%llu)\n",
         dat.size, GPU_BOUND_SIZE);
      return -EINVAL;
   }

   // Check if another PID already owns this GpuAsyncCore state
   pid_t pid = atomic64_cmpxchg(&rdmaData->pid, 0, current->pid);
   if (pid != 0 && pid != current->pid) {
      dev_warn(dev->device, "NvP2P_AddNvidia: error: Calling PID (%d) GpuAsyncCore state already locked by PID %d\n",
               current->pid, pid);
      return -EBUSY;
   }

   // Set buffer pointers based on the operation mode (write/read)
   if (dat.write) {
      if (rdmaData->writeBufferCount >= rdmaData->maxBuffers) {
         dev_warn(dev->device, "NvP2P_AddNvidia: Too many write buffers: max %u\n", rdmaData->maxBuffers);
         atomic64_set(&rdmaData->pid, 0);
         return -EINVAL;
      }
      buffer = &(data->writeBuffers.list[rdmaData->writeBufferCount]);
   } else {
      if (rdmaData->readBufferCount >= rdmaData->maxBuffers) {
         dev_warn(dev->device, "NvP2P_AddNvidia: Too many read buffers: max %u\n", rdmaData->maxBuffers);
         atomic64_set(&rdmaData->pid, 0);
         return -EINVAL;
      }
      buffer = &(data->readBuffers.list[rdmaData->readBufferCount]);
   }

   // Initialize buffer properties
   buffer->write = dat.write;
   buffer->address = dat.address;
   buffer->size = dat.size;
   buffer->pageTable = 0;
   buffer->dmaMapping = 0;
   buffer->dev = dev;

   // Align virtual start address as required by NVIDIA kernel driver
   virt_start = buffer->address & GPU_BOUND_MASK;

   // Handle addresses that aren't aligned to 64k boundary. CUDA doesn't have an easy way to perform aligned allocations, so
   // account for that here.
   virt_offset = buffer->address & ~GPU_BOUND_MASK;

   // Align pin size to page boundary (64k)
   pin_size = (buffer->address + buffer->size - virt_start + GPU_BOUND_OFFSET) & GPU_BOUND_MASK;

   dev_warn(dev->device, "NvP2P_AddNvidia: attempting to map. address=0x%llx, size=%i, virt_start=0x%llx, pin_size=%li, write=%i\n",
         buffer->address, buffer->size, virt_start, pin_size, buffer->write);

   // Map GPU memory through NVIDIA P2P API
   ret = nvidia_p2p_get_pages(0, 0, virt_start, pin_size, &(buffer->pageTable), NvP2P_FreeNvidia, buffer);

   if (ret == 0) {
      dev_warn(dev->device, "NvP2P_AddNvidia: mapped memory with address=0x%llx, size=%i, page count=%i, write=%i\n", buffer->address, buffer->size, buffer->pageTable->entries, buffer->write);

      // DMA map the pages
      ret = nvidia_p2p_dma_map_pages(dev->pcidev, buffer->pageTable, &(buffer->dmaMapping));
      dev_warn(dev->device, "NvP2P_AddNvidia: dma map done. ret = %i\n", ret);

      if (ret != 0) {
         dev_warn(dev->device, "NvP2P_AddNvidia: error mapping page tables ret=%i\n", ret);
      } else {
         // Determine how much memory is contiguous
         mapSize = 0;
         for (x=0; x < buffer->dmaMapping->entries; x++) {
            if (buffer->dmaMapping->dma_addresses[0] + mapSize == buffer->dmaMapping->dma_addresses[x]) {
               mapSize += GPU_BOUND_SIZE;
            } else {
               break;
            }
         }

         // Special case for when dat.size is not 64k aligned
         if (mapSize > dat.size)
            mapSize = dat.size;

         dma_address = buffer->dmaMapping->dma_addresses[0] + virt_offset;

         if (x < buffer->dmaMapping->entries) {
            dev_warn(dev->device, "NvP2P_AddNvidia: non-contiguous GPU memory detected: requested %d pages, only got %i pages\n", buffer->dmaMapping->entries, x);
         }

         dev_warn(dev->device, "NvP2P_AddNvidia: dma address 0 = 0x%llx, adjusted dma address 0 = 0x%llx, total = %li, pages = %i\n",
               buffer->dmaMapping->dma_addresses[0], dma_address, mapSize, x);

         // Update buffer count and write DMA addresses to device
         if (buffer->write) {
            // Bit of a hack to catch V4+ API misuses. Since v4 has only one maxSize register, it needs to match for all buffers
            minSize = readGpuAsyncReg(rdmaData->base, &GpuAsyncReg_RemoteWriteMaxSizeV4);
            if (minSize > 1 && minSize != mapSize) {
               dev_warn(dev->device, "NvP2P_AddNvidia: mapSize=%zu does not match last configured mapSize of %zu. Write buffers must all be identically sized\n",
                  minSize, mapSize);
               atomic64_set(&rdmaData->pid, 0);
               return -EINVAL;
            }

            // Compute version specific offsets
            if (rdmaData->version < 4) {
               offset = GPU_ASYNC_REG_WRITE_BASE_V1 + data->writeBuffers.count * 16;
            } else {
               offset = GPU_ASYNC_REG_WRITE_BASE_V4 + data->writeBuffers.count * 8;
            }

            writel(dma_address & 0xFFFFFFFF, rdmaData->base + offset);
            writel((dma_address >> 32) & 0xFFFFFFFF, rdmaData->base + offset + 0x4);

            if (rdmaData->version < 4) {
               writel(mapSize, rdmaData->base + GPU_ASYNC_REG_WRITE_BASE_V1 + data->writeBuffers.count * 16ULL + 0x8);
            } else {
               writeGpuAsyncReg(rdmaData->base, &GpuAsyncReg_RemoteWriteMaxSizeV4, mapSize);
            }
            data->writeBuffers.count++;
         } else {
            // Compute version specific offsets
            if (rdmaData->version < 4) {
               offset = GPU_ASYNC_REG_READ_BASE_V1 + data->readBuffers.count * 16;
            } else {
               offset = GPU_ASYNC_REG_READ_BASE_V4 + data->readBuffers.count * 8;
            }

            writel(dma_address & 0xFFFFFFFF, rdmaData->base + offset);
            writel((dma_address >> 32) & 0xFFFFFFFF, rdmaData->base + offset + 0x4);
            data->readBuffers.count++;
         }
      }
   } else {
      dev_warn(dev->device, "NvP2P_AddNvidia: failed to pin memory with address=0x%llx. ret=%i\n", dat.address, ret);
      atomic64_set(&rdmaData->pid, 0);
      return -1;
   }

   x = 0;

   if (rdmaData->writeBufferCount > 0) {
      if (rdmaData->version < 4) {
         x |= 0x00000100;  // Set write-enable bit
         x |= (data->writeBuffers.count-1);  // Set the 0-based write buffer count
      } else {
         x &= ~(1 << 15);  // Clear write-enable bit; User space must call gpuEnableTx to set this.
         x |= (data->writeBuffers.count-1) & 0x7FFF;  // Set the 0-based write buffer count
      }
   }

   if (rdmaData->readBufferCount > 0) {
      if (rdmaData->version < 4) {
         x |= 0x01000000;  // Set read-enable bit
         x |= (data->readBuffers.count-1) << 16;  // Set the 0-based read buffer count
      } else {
         x &= ~(1 << 31);  // Clear read-enable bit; User space must call gpuEnableRx to set this.
         x |= (data->readBuffers.count-1) << 16;  // Set the 0-based read buffer count
      }
   }

   rdmaData->disabled = 0;
   writel(x, rdmaData->base+0x008);
   return 0;
}


/**
 * NvP2P_RemNvidia - Remove NVIDIA GPU memory mappings
 * @dev: pointer to the DMA device structure
 * @arg: argument specifying additional command or data (unused in this function)
 *
 * This function unmaps the write and read buffer memory previously mapped for NVIDIA GPU,
 * using the NVIDIA Peer-to-Peer (P2P) DMA API. It iterates over the write and read buffers,
 * unmaps each using nvidia_p2p_dma_unmap_pages, and releases the pages with nvidia_p2p_put_pages.
 * Finally, it resets the buffers' count and disables a specific hardware functionality
 * by writing to a register.
 *
 * Return: Always returns 0 indicating success.
 */
int32_t NvP2P_RemNvidia(struct DmaDevice *dev, uint64_t arg) {
   uint32_t x;
   int ret;

   struct RdmaData *rdmaData;
   struct NvP2PData *data;
   struct GpuBuffer *buffer;

   // Retrieve the GPU specific data from the DMA device
   rdmaData = (struct RdmaData *)dev->rdmaData;
   data = NvP2P_GetPvt(dev);

   dev_info(dev->device, "NvP2P_RemNvidia: Called\n");

   // Ensure the calling PID actually owns the state
   pid_t pid = atomic64_cmpxchg(&rdmaData->pid, current->pid, 0);
   if (pid != current->pid) {
      dev_warn(dev->device, "NvP2P_RemNvidia: Called by PID (%d) that doesn't own the GpuAsyncCore state!\n",
               current->pid);
      return -EBUSY;
   }

   // Clear out FPGA state, disable DMAs
   Rdma_ClearBufferRegs(dev);

   // Unmap write pages
   for (x = 0; x < rdmaData->writeBufferCount; x++) {
      buffer = &(data->writeBuffers.list[x]);

      ret = nvidia_p2p_dma_unmap_pages(dev->pcidev, buffer->pageTable, buffer->dmaMapping);
      if (ret != 0) {
         dev_warn(dev->device, "NvP2P_RemNvidia: nvidia_p2p_dma_unmap_pages returned %d\n", ret);
      }
   }

   // Unmap read pages
   for (x = 0; x < rdmaData->readBufferCount; x++) {
      buffer = &(data->readBuffers.list[x]);

      ret = nvidia_p2p_dma_unmap_pages(dev->pcidev, buffer->pageTable, buffer->dmaMapping);
      if (ret != 0) {
         dev_warn(dev->device, "NvP2P_RemNvidia: nvidia_p2p_dma_unmap_pages returned %d\n", ret);
      }
   }

   return 0;
}

/**
 * NvP2P_FreeNvidia - Release NVIDIA GPU resources.
 * @data: Pointer to the device-specific data
 *
 * This is called for each buffer by the unpin callback, and frees the page table.
 * Pages are unmapped by explicit calls to NvP2P_RemNvidia, or automatically when the
 * device fd is closed/removed.
 */
void NvP2P_FreeNvidia(void *data) {
   int r;
   struct GpuBuffer *buffer = data;

   // Disable DMAs, clear out registers on the FPGA side.
   Rdma_ClearBufferRegs(buffer->dev);

   // Free the underlying page table
   if ((r = nvidia_p2p_free_page_table(buffer->pageTable)) != 0) {
      dev_warn(buffer->dev->device, "NvP2P_FreeNvidia: nvidia_p2p_free_page_table returned %d!\n", r);
   }
   buffer->pageTable = NULL;
}

void NvP2P_Shutdown(struct DmaDevice* dev) {
   
}