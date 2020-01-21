/**
 *-----------------------------------------------------------------------------
 * Title      : AXIS Gen2 Functions
 * ----------------------------------------------------------------------------
 * File       : axis_gen2.c
 * Created    : 2017-02-03
 * ----------------------------------------------------------------------------
 * Description:
 * Access functions for Gen2 AXIS DMA
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

// Execute command
void Gpu_Init(struct DmaDevice *dev, uint32_t offset) {
   struct GpuData * gpuData;

   // Setup GPU Utility
   gpuData = (struct GpuData *)kmalloc(sizeof(struct GpuData),GFP_KERNEL);
   dev->utilData = gpuData;

   gpuData->base = dev->base + offset;
   gpuData->writeBuffers.count = 0;
   gpuData->readBuffers.count = 0;
}

// Execute command
int32_t Gpu_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   switch (cmd) {

      // Add NVIDIA Memory
      case GPU_Add_Nvidia_Memory:
         return(Gpu_AddNvidia(dev,arg));
         break;

      // Rem NVIDIA Memory
      case GPU_Rem_Nvidia_Memory:
         return(Gpu_RemNvidia(dev,arg));
         break;

      default:
         dev_warn(dev->device,"Command: Invalid command=%i\n",cmd); 
         return(-1);
         break;
   }
}

// Add NVIDIA Memory
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

   if ((ret = copy_from_user(&dat,(void *)arg,sizeof(struct GpuNvidiaData)))) {
      dev_warn(dev->device,"Gpu_AddNvidia: copy_from_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &dat);
      return(-1);
   }

   if (!dat.size) return -EINVAL;

   // Set pointers
   if (dat.write) buffer = &(data->writeBuffers.list[data->writeBuffers.count]);
   else buffer = &(data->readBuffers.list[data->readBuffers.count]);

   buffer->write   = dat.write;
   buffer->address = dat.address;
   buffer->size    = dat.size;
   buffer->pageTable  = 0;
   buffer->dmaMapping = 0;

   // do proper alignment, as required by NVIDIA kernel driver
   virt_start = buffer->address & GPU_BOUND_MASK;
   pin_size = buffer->address + buffer->size - virt_start;

   dev_warn(dev->device,"Gpu_AddNvidia: attemping to map. address=0x%llx, size=%i, virt_start=0x%llx, pin_size=%li, write=%i\n",
         buffer->address,buffer->size,virt_start,pin_size,buffer->write);

   ret = nvidia_p2p_get_pages(0, 0, virt_start, pin_size, &(buffer->pageTable), Gpu_FreeNvidia, dev);

   if (ret == 0) {
      dev_warn(dev->device,"Gpu_AddNvidia: mapped memory with address=0x%llx, size=%i, page count=%i, write=%i\n",buffer->address,buffer->size,buffer->pageTable->entries,buffer->write);

      ret = nvidia_p2p_dma_map_pages(dev->pcidev, buffer->pageTable, &(buffer->dmaMapping));
      dev_warn(dev->device,"Gpu_AddNvidia: dma map done. ret = %i\n",ret);

      if (ret != 0) {
         dev_warn(dev->device,"Gpu_AddNvidia: error mapping page tables ret=%i\n",ret);
      }
      else {

         // Determine how much memory is contiguous
         mapSize = 0;
         for (x=0; x < buffer->dmaMapping->entries; x++) {
            if (buffer->dmaMapping->dma_addresses[0] + mapSize == buffer->dmaMapping->dma_addresses[x] )
               mapSize += GPU_BOUND_SIZE;
            else break;
         }

         dev_warn(dev->device,"Gpu_AddNvidia: dma address 0 = 0x%llx, total = %li, pages = %i\n", 
               buffer->dmaMapping->dma_addresses[0],mapSize,x);

         if (buffer->write){
            iowrite32(buffer->dmaMapping->dma_addresses[0], data->base+0x100+data->writeBuffers.count*16);
            iowrite32(mapSize,data->base+0x108+data->writeBuffers.count*16);
            data->writeBuffers.count++;
         } else {
            iowrite32(buffer->dmaMapping->dma_addresses[0], data->base+0x200+data->readBuffers.count*16);
            data->readBuffers.count++;
         }
      }
   } else {
       dev_warn(dev->device,"Gpu_AddNvidia: failed to pin memory with address=0x%llx. ret=%i\n", dat.address,ret);
       return(-1);
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

   iowrite32(x,data->base+0x008);
   return(0);
}

// REm NVIDIA Memory
int32_t Gpu_RemNvidia(struct DmaDevice *dev, uint64_t arg) {
   uint32_t x;
   u64      virt_start;

   struct GpuData * data;
   struct GpuBuffer * buffer;

   data = (struct GpuData *)dev->utilData;

   for (x=0; x < data->writeBuffers.count; x++) {
      buffer = &(data->writeBuffers.list[x]);

      virt_start = buffer->address & GPU_BOUND_MASK;

      nvidia_p2p_dma_unmap_pages(dev->pcidev, buffer->pageTable, buffer->dmaMapping);
      nvidia_p2p_put_pages(0, 0, virt_start, buffer->pageTable);

      dev_warn(dev->device,"Gpu_AddNvidia: unmapped write memory with address=0x%llx\n", buffer->address);
   }

   for (x=0; x < data->readBuffers.count; x++) {
      buffer = &(data->readBuffers.list[x]);

      virt_start = buffer->address & GPU_BOUND_MASK;

      nvidia_p2p_dma_unmap_pages(dev->pcidev, buffer->pageTable, buffer->dmaMapping);
      nvidia_p2p_put_pages(0, 0, virt_start, buffer->pageTable);

      dev_warn(dev->device,"Gpu_AddNvidia: unmapped read memory with address=0x%llx\n", buffer->address);
   }

   data->writeBuffers.count = 0;
   data->readBuffers.count  = 0;
   iowrite32(0,data->base+0x008);
   return(0);
}

// NVIDIA Callback
void Gpu_FreeNvidia(void *data) {
   struct DmaDevice * dev = (struct DmaDevice *)data;
   dev_warn(dev->device,"Axis_FreeNvidia: Called\n");
   Gpu_RemNvidia(dev,0);
}

