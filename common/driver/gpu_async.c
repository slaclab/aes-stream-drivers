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
#include <GpuAsyncRegs.h>
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

   uint8_t* gpuBase = dev->base + offset;
   dev->gpuEn = !!readGpuAsyncReg(gpuBase, &GpuAsyncReg_Version);

   /* GPU not enabled, avoid allocating GPU data */
   if (!dev->gpuEn)
      return;

   /* Allocate memory for GPU utility data */
   gpuData = (struct GpuData *)kzalloc(sizeof(struct GpuData), GFP_KERNEL);
   if (!gpuData)
      return;  // Handle memory allocation failure if necessary

   /* Associate GPU utility data with the device */
   dev->utilData = gpuData;

   /* Initialize GPU base address and buffer counts */
   gpuData->base = dev->base + offset;
   gpuData->writeBuffers.count = 0;
   gpuData->readBuffers.count = 0;
   gpuData->offset = offset;
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

      // Set write enable flag
      case GPU_Set_Write_Enable:
         return Gpu_SetWriteEn(dev, arg);
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
   u64     virt_start, virt_offset, dma_address;
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

   if ((dat.size & ~GPU_BOUND_MASK) != 0) {
      dev_warn(dev->device, "Gpu_AddNvidia: error: memory size (%u) is not a multiple of GPU page size (%llu)\n",
         dat.size, GPU_BOUND_SIZE);
      return -EINVAL;
   }

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

   // Handle addresses that aren't aligned to 64k boundary. CUDA doesn't have an easy way to perform aligned allocations, so
   // account for that here.
   virt_offset = buffer->address & ~GPU_BOUND_MASK;

   // Align pin size to page boundary (64k)
   pin_size = (buffer->address + buffer->size - virt_start + GPU_BOUND_OFFSET) & GPU_BOUND_MASK;

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
         dev_warn(dev->device, "Gpu_AddNvidia: error mapping page tables ret=%i\n", ret);

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
            dev_warn(dev->device, "Gpu_AddNvidia: non-contiguous GPU memory detected: requested %d pages, only got %i pages\n", buffer->dmaMapping->entries, x);
         }

         dev_warn(dev->device, "Gpu_AddNvidia: dma address 0 = 0x%llx, adjusted dma address 0 = 0x%llx, total = %li, pages = %i\n",
               buffer->dmaMapping->dma_addresses[0], dma_address, mapSize, x);

         // Update buffer count and write DMA addresses to device
         if (buffer->write) {
            writel(dma_address & 0xFFFFFFFF, data->base+0x100+data->writeBuffers.count*16);
            writel((dma_address >> 32) & 0xFFFFFFFF, data->base+0x104+data->writeBuffers.count*16);
            writel(mapSize, data->base+0x108+data->writeBuffers.count*16);
            data->writeBuffers.count++;
         } else {
            writel(dma_address & 0xFFFFFFFF, data->base+0x200+data->readBuffers.count*16);
            writel((dma_address >> 32) & 0xFFFFFFFF, data->base+0x204+data->readBuffers.count*16);
            data->readBuffers.count++;
         }
      }
   } else {
       dev_warn(dev->device, "Gpu_AddNvidia: failed to pin memory with address=0x%llx. ret=%i\n", dat.address, ret);
       return -1;
   }

   x = 0;

   if (data->writeBuffers.count > 0) {
      x |= 0x00000100;  // Set write-enable bit
      x |= (data->writeBuffers.count-1);  // Set the 0-based write buffer count
   }

   if (data->readBuffers.count > 0) {
      x |= 0x01000000;  // Set read-enable bit
      x |= (data->readBuffers.count-1) << 16;  // Set the 0-based read buffer count
   }

   writel(x, data->base+0x008);
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

/**
 * Gpu_SetWriteEn - Set write enable for buffer
 * @dev: pointer to the DMA device structure
 * @arg: user space argument pointing to buffer index
 *
 * This function enables a DMA buffer for DMA operations.
 *
 * Return: 0 on success, negative error code on failure.
 */
int32_t Gpu_SetWriteEn(struct DmaDevice *dev, uint64_t arg) {
   uint32_t idx;
   int32_t ret;

   struct GpuData   * data;

   data = (struct GpuData *)dev->utilData;

   // Copy data from user space
   if ((ret = copy_from_user(&idx, (void *)arg, sizeof(uint32_t)))) {
      dev_warn(dev->device, "Gpu_SetWriteEn: copy_from_user failed. ret=%i, user=%p\n", ret, (void *)arg);
      return -1;
   }

   if ( idx >= data->writeBuffers.count ) {
      dev_warn(dev->device, "Gpu_SetWriteEn: Invalid write buffer index idx=%i, count=%i\n", idx, data->writeBuffers.count);
      return -1;
   }

   writel(0x1, data->base+0x300+idx*4);

   return 0;
}

/**
 * Gpu_Show - Show information about DataGpu internal state
 * @s: Sequence file pointer to write to
 * @dev: Device to read from
 */
void Gpu_Show(struct seq_file *s, struct DmaDevice *dev) {
   u32 i;
   struct GpuData* data = (struct GpuData*)dev->utilData;

   const u32 version = readGpuAsyncReg(data->base, &GpuAsyncReg_Version);
   const u32 readBuffCnt = readGpuAsyncReg(data->base, &GpuAsyncReg_ReadCount)+1;
   const u32 writeBuffCnt = readGpuAsyncReg(data->base, &GpuAsyncReg_WriteCount)+1;
   const u32 writeEnable = readGpuAsyncReg(data->base, &GpuAsyncReg_WriteEnable);
   const u32 readEnable = readGpuAsyncReg(data->base, &GpuAsyncReg_ReadEnable);

   seq_printf(s, "\n---------------- DataGPU State ----------------\n");
   seq_printf(s, "    GpuAsyncCore Offset : 0x%X\n", data->offset);
   seq_printf(s, "   GpuAsyncCore Version : %d\n", version);
   seq_printf(s, "            Max Buffers : %u\n", readGpuAsyncReg(data->base, &GpuAsyncReg_MaxBuffers));
   seq_printf(s, "     Write Buffer Count : %u\n", writeBuffCnt);
   seq_printf(s, "           Write Enable : %u\n", writeEnable);
   seq_printf(s, "      Read Buffer Count : %u\n", readBuffCnt);
   seq_printf(s, "            Read Enable : %u\n", readEnable);
   seq_printf(s, "         RX Frame Count : %u\n", readGpuAsyncReg(data->base, &GpuAsyncReg_RxFrameCnt));
   seq_printf(s, "         TX Frame Count : %u\n", readGpuAsyncReg(data->base, &GpuAsyncReg_TxFrameCnt));
   seq_printf(s, "  AXI Write Error Count : %u\n", readGpuAsyncReg(data->base, &GpuAsyncReg_AxiWriteErrorCnt));
   if (version >= 2)  // Added in V2
      seq_printf(s, "AXI Write Timeout Count : %u\n", readGpuAsyncReg(data->base, &GpuAsyncReg_AxiWriteTimeoutCnt));
   seq_printf(s, "   AXI Read Error Count : %u\n", readGpuAsyncReg(data->base, &GpuAsyncReg_AxiReadErrorCnt));

   for (i = 0; i < writeBuffCnt && writeEnable; ++i) {
      u32 wal = readl(data->base + GPU_ASYNC_REG_WRITE_ADDR_L_OFFSET(i));
      u32 wah = readl(data->base + GPU_ASYNC_REG_WRITE_ADDR_H_OFFSET(i));
      seq_printf(s, "\n-------- Write Buffer %u --------\n", i);
      seq_printf(s, "  Write Address : 0x%llX\n", ((u64)wah << 32) | wal);
      seq_printf(s, "     Write Size : 0x%X\n", readl(data->base + GPU_ASYNC_REG_WRITE_SIZE_OFFSET(i)));
   }

   for (i = 0; i < readBuffCnt && readEnable; ++i) {
      u32 ral = readl(data->base + GPU_ASYNC_REG_READ_ADDR_L_OFFSET(i));
      u32 rah = readl(data->base + GPU_ASYNC_REG_READ_ADDR_H_OFFSET(i));
      seq_printf(s, "\n-------- Read Buffer %u --------\n", i);
      seq_printf(s, "  Read Address : 0x%llX\n", ((u64)rah << 32) | ral);
      seq_printf(s, "     Read Size : 0x%X\n", readl(data->base + GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET(i)));
   }
}
