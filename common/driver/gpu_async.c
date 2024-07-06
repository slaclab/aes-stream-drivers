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

#include <gpu_async.h>
#include <GpuAsync.h>
#include <linux/seq_file.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <nv-p2p.h>

/**
 * Gpu_Init - Initialize GPU with given offset
 * @dev: pointer to the DmaDevice structure
 * @offset: memory offset for GPU initialization
 *
 * This function allocates memory for GpuData structure, initializes
 * it, and associates it with the given DmaDevice. It sets up
 * the base address for GPU operations and initializes buffer counts.
 */
void Gpu_Init(struct DmaDevice *dev, uint32_t offset) {
   struct GpuData *gpuData;

   /* Allocate memory for GPU utility data */
   gpuData = (struct GpuData *)kzalloc(sizeof(struct GpuData), GFP_KERNEL);
   if (!gpuData)
      return; // Handle memory allocation failure if necessary

   /* Associate GPU utility data with the device */
   dev->utilData = gpuData;

   /* Initialize GPU base address and buffer counts */
   gpuData->base = dev->base + offset;
   gpuData->writeBuffers.count = 0;
   gpuData->readBuffers.count = 0;
}

/**
 * Gpu_Command - Execute command on GPU.
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
int32_t Gpu_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   switch (cmd) {
      // Add NVIDIA Memory
      case GPU_Add_Nvidia_Memory:
         return Gpu_AddNvidia(dev, arg);
         break;

      // Remove NVIDIA Memory
      case GPU_Rem_Nvidia_Memory:
         return Gpu_RemNvidia(dev, arg);
         break;

      default:
         dev_warn(dev->device, "Command: Invalid command=%u\n", cmd);
         return -1;
         break;
   }
}

/**
 * Gpu_AddNvidia - Add NVIDIA GPU memory to the device
 * @dev: pointer to the DMA device structure
 * @arg: user space argument pointing to GpuNvidiaData structure
 *
 * This function adds NVIDIA GPU memory for DMA operations. It involves
 * copying data from user space, validating it, and setting up DMA mappings
 * through NVIDIA's Peer-to-Peer (P2P) API.
 *
 * Return: 0 on success, negative error code on failure.
 */
int32_t Gpu_AddNvidia(struct DmaDevice *dev, uint64_t arg) {
   int32_t ret;
   uint32_t x;
   u64     virt_start;
   size_t  pin_size;
   size_t  mapSize;

   struct GpuData   * data;
   struct GpuBuffer * buffer;
   struct GpuNvidiaData dat;

   data = (struct GpuData *)dev->utilData;

   // Copy data from user space
   if ((ret = copy_from_user(&dat, (void *)arg, sizeof(struct GpuNvidiaData)))) {
      dev_warn(dev->device, "Gpu_AddNvidia: copy_from_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &dat);
      return -1;
   }

   if (!dat.size) return -EINVAL;

   // Set buffer pointers based on the operation mode (write/read)
   if (dat.write)
      buffer = &(data->writeBuffers.list[data->writeBuffers.count]);
   else
      buffer = &(data->readBuffers.list[data->readBuffers.count]);

   // Initialize buffer properties
   buffer->write = dat.write;
   buffer->address = dat.address;
   buffer->size = dat.size;
   buffer->pageTable = 0;
   buffer->dmaMapping = 0;

   // Align virtual start address as required by NVIDIA kernel driver
   virt_start = buffer->address & GPU_BOUND_MASK;
   pin_size = buffer->address + buffer->size - virt_start;

   dev_warn(dev->device, "Gpu_AddNvidia: attempting to map. address=0x%llx, size=%i, virt_start=0x%llx, pin_size=%li, write=%i\n",
         buffer->address, buffer->size, virt_start, pin_size, buffer->write);

   // Map GPU memory through NVIDIA P2P API
   ret = nvidia_p2p_get_pages(0, 0, virt_start, pin_size, &(buffer->pageTable), Gpu_FreeNvidia, dev);

   if (ret == 0) {
      dev_warn(dev->device, "Gpu_AddNvidia: mapped memory with address=0x%llx, size=%i, page count=%i, write=%i\n", buffer->address, buffer->size, buffer->pageTable->entries, buffer->write);

      // DMA map the pages
      ret = nvidia_p2p_dma_map_pages(dev->pcidev, buffer->pageTable, &(buffer->dmaMapping));
      dev_warn(dev->device, "Gpu_AddNvidia: dma map done. ret = %i\n", ret);

      if (ret != 0) {
         dev_warn(dev->device,"Gpu_AddNvidia: error mapping page tables ret=%i\n",ret);

      } else {

         // Determine how much memory is contiguous
         mapSize = 0;
         for (x=0; x < buffer->dmaMapping->entries; x++) {
            if (buffer->dmaMapping->dma_addresses[0] + mapSize == buffer->dmaMapping->dma_addresses[x] )
               mapSize += GPU_BOUND_SIZE;
            else break;
         }

         dev_warn(dev->device, "Gpu_AddNvidia: dma address 0 = 0x%llx, total = %li, pages = %i\n",
               buffer->dmaMapping->dma_addresses[0], mapSize, x);

         // Update buffer count and write DMA addresses to device
         if (buffer->write) {
            writel(buffer->dmaMapping->dma_addresses[0] & 0xFFFFFFFF, data->base+0x100+data->writeBuffers.count*16);
            writel((buffer->dmaMapping->dma_addresses[0] >> 32) & 0xFFFFFFFF, data->base+0x104+data->writeBuffers.count*16);
            writel(mapSize,data->base+0x108+data->writeBuffers.count*16);
            data->writeBuffers.count++;
         } else {
            writel(buffer->dmaMapping->dma_addresses[0] & 0xFFFFFFFF, data->base+0x200+data->readBuffers.count*16);
            writel((buffer->dmaMapping->dma_addresses[0] >> 32) & 0xFFFFFFFF, data->base+0x204+data->readBuffers.count*16);
            data->readBuffers.count++;
         }
      }
   } else {
       dev_warn(dev->device,"Gpu_AddNvidia: failed to pin memory with address=0x%llx. ret=%i\n", dat.address,ret);
       return -1;
   }

   x = 0;

   if (data->writeBuffers.count > 0 ) {
      x |= 0x00000100;
      x |= (data->writeBuffers.count-1);
   }

   if (data->readBuffers.count > 0 ) {
      x |= 0x01000000;
      x |= (data->readBuffers.count-1) << 16;
   }

   writel(x,data->base+0x008);
   return 0;
}

/**
 * Gpu_RemNvidia - Remove NVIDIA GPU memory mappings
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
int32_t Gpu_RemNvidia(struct DmaDevice *dev, uint64_t arg) {
   uint32_t x;
   u64 virt_start;

   struct GpuData *data;
   struct GpuBuffer *buffer;

   // Retrieve the GPU specific data from the DMA device
   data = (struct GpuData *)dev->utilData;

   // Unmap and release pages for all write buffers
   for (x = 0; x < data->writeBuffers.count; x++) {
      buffer = &(data->writeBuffers.list[x]);
      virt_start = buffer->address & GPU_BOUND_MASK;

      nvidia_p2p_dma_unmap_pages(dev->pcidev, buffer->pageTable, buffer->dmaMapping);
      nvidia_p2p_free_page_table(buffer->pageTable);

      dev_warn(dev->device, "Gpu_RemNvidia: unmapped write memory with address=0x%llx\n", buffer->address);
   }

   // Unmap and release pages for all read buffers
   for (x = 0; x < data->readBuffers.count; x++) {
      buffer = &(data->readBuffers.list[x]);
      virt_start = buffer->address & GPU_BOUND_MASK;

      nvidia_p2p_dma_unmap_pages(dev->pcidev, buffer->pageTable, buffer->dmaMapping);
      nvidia_p2p_free_page_table(buffer->pageTable);

      dev_warn(dev->device, "Gpu_RemNvidia: unmapped read memory with address=0x%llx\n", buffer->address);
   }

   // Reset the buffer counts and disable specific functionality by writing to a register
   data->writeBuffers.count = 0;
   data->readBuffers.count = 0;
   writel(0, data->base + 0x008);

   return 0;
}

/**
 * Gpu_FreeNvidia - Release NVIDIA GPU resources
 * @data: Pointer to the device-specific data
 *
 * This function is a callback for freeing NVIDIA GPU resources associated
 * with a DMA device. It logs a warning message and removes NVIDIA GPU
 * resources.
 */
void Gpu_FreeNvidia(void *data) {
   struct DmaDevice *dev = (struct DmaDevice *)data;
   dev_warn(dev->device, "Gpu_FreeNvidia: Called\n");
   Gpu_RemNvidia(dev, 0);
}
