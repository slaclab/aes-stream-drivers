/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Provides a generic RDMA interface using the Linux dmabuf
 *  infrastructure. Some of the naming in here involves "GPUs", but that is
 *  only because the firmware was originally designed with NVIDIA's GpuDirect
 *  RDMA technology in mind. However, GpuAsyncCore can operate with any physical
 *  address.
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

#include <dma_common.h>
#include <rdma_common.h>
#include <data_dev_top.h>
#include <rdma_dma_buf.h>
#include <GpuAsyncRegs.h>
#include <GpuAsync.h>

#include <linux/dma-buf.h>
#include <linux/mutex.h>
#include <linux/time.h>
#include <linux/delay.h>

struct DmaBufBuffer {
   struct dma_buf* dma_buf;
   struct dma_buf_attachment* dma_attach;
   struct sg_table* tab;
   dma_addr_t phys_base;
   enum dma_data_direction dir;
};

struct DmaBufData {
   struct DmaBufBuffer* writeBuffs;
   struct DmaBufBuffer* readBuffs;
   struct mutex lock;
};

static int DmaBuf_RemoveBuf(struct DmaDevice* dev, struct DmaBufBuffer* buffer);

static struct DmaBufData* DmaBuf_GetPvt(struct DmaDevice* dev) {
   struct RdmaData* rd = dev->rdmaData;
   if (!rd)
      return NULL;
   return rd->dmaBufData;
}

void* DmaBuf_Init(struct DmaDevice* dev, uint32_t offset) {
   void* gpuBase = dev->base + offset;
   uint8_t version = readGpuAsyncReg(gpuBase, &GpuAsyncReg_Version);
   if (!version)
      return 0; /* Not supported by firmware; not an error! */

   struct DmaBufData* info = kzalloc(sizeof(struct DmaBufData), GFP_KERNEL);
   if (!info) {
      dev_warn(dev->device, "DmaBuf_Init: DmaBufData allocation failed\n");
      return NULL;
   }

   info->writeBuffs = kzalloc(sizeof(struct DmaBufBuffer) * MAX_GPU_BUFFERS, GFP_KERNEL);
   info->readBuffs = kzalloc(sizeof(struct DmaBufBuffer) * MAX_GPU_BUFFERS, GFP_KERNEL);
   if (!info->writeBuffs || !info->readBuffs) {
      dev_warn(dev->device, "DmaBuf_Init: Failed to allocate buffer lists\n");
      kfree(info->writeBuffs);
      kfree(info->readBuffs);
      kfree(info);
      return NULL;
   }

   mutex_init(&info->lock);

   return info;
}

void DmaBuf_Shutdown(struct DmaDevice* dev) {
   struct DmaBufData* info = DmaBuf_GetPvt(dev);
   if (!info) {
      return;
   }
   
   struct RdmaData* rdmaData = dev->rdmaData;

   mutex_lock(&info->lock);

   /* Release all remaining buffers */
   for (int i = 0; i < rdmaData->writeBufferCount; ++i) {
      DmaBuf_RemoveBuf(dev, &info->writeBuffs[i]);
   }

   for (int i = 0; i < rdmaData->readBufferCount; ++i) {
      DmaBuf_RemoveBuf(dev, &info->readBuffs[i]);
   }

   mutex_unlock(&info->lock);

   mutex_destroy(&info->lock);
   kfree(info->readBuffs);
   kfree(info->writeBuffs);
   kfree(info);
   dev->rdmaData = NULL;
}

static void axi_rdma_move_notify(struct dma_buf_attachment* att) {
   dev_warn(att->dev, "Move notify called");
}

static const struct dma_buf_attach_ops importer_ops = {
   .allow_peer2peer = 1,
   .move_notify = axi_rdma_move_notify,
};

static int DmaBuf_AddBuf(struct DmaDevice* dev, int buffd, int write) {
   struct dma_buf* buf = ERR_PTR(-1);
   struct dma_buf_attachment* attach = ERR_PTR(-1);
   struct sg_table* tab = ERR_PTR(-1);
   struct DmaBufBuffer* buffer = ERR_PTR(-1);
   int ret = 0;

   struct DmaBufData* info = DmaBuf_GetPvt(dev);
   if (!info) {
      return -ENOTSUPP;
   }
   struct RdmaData* rdmaData = dev->rdmaData;

   mutex_lock(&info->lock);

   /* Ensure we don't have too many buffers */
   uint32_t* count = write ? (&rdmaData->writeBufferCount) : (&rdmaData->readBufferCount);
   if (*count >= MAX_GPU_BUFFERS) {
      mutex_unlock(&info->lock);
      return -EAGAIN;
   }

   buffer = write ? &info->writeBuffs[*count] : &info->readBuffs[*count];

   buf = dma_buf_get(buffd);
   if (IS_ERR(buf)) {
      dev_warn(dev->device, "DmaBuf_AddBuf: Invalid dmabuf: %ld\n", PTR_ERR(buf));
      ret = -EINVAL;
      goto error;
   }

   dev_warn(dev->device, "DmaBuf_AddBuf: exporter=%s, size=0x%lX\n", buf->exp_name, buf->size);

   /* Attach to the buffer to get ready for DMA */
   attach = dma_buf_dynamic_attach(buf, dev->device, &importer_ops, info);
   if (IS_ERR(attach)) {
      dev_warn(dev->device, "DmaBuf_AddBuf: Failed to attach to buffer: %ld\n", PTR_ERR(tab));
      ret = -EINVAL;
      goto error;
   }

   /* Attempt to map the attachment for DMA access */
   buffer->dir = write ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
   if (IS_ERR(tab = dma_buf_map_attachment(attach, buffer->dir))) {
      dev_warn(dev->device, "DmaBuf_AddBuf: Unable to map attachment for access: %ld\n", PTR_ERR(tab));
      ret = -EFAULT;
      goto error;
   }

   /* Check for physically contiguous memory. It seems like cudaMalloc/cuMemAlloc will always give us a block of
    * memory that is contiguous, aligned to buffer size or GPU page size (maybe?) */
   struct sg_dma_page_iter iter;
   dma_addr_t addr = 0, phys_base = sg_dma_address(tab->sgl);
   size_t size = 0, nents = 0;
   for_each_sg_dma_page(tab->sgl, &iter, tab->nents, 0) {
      if (addr && addr + PAGE_SIZE != sg_page_iter_dma_address(&iter)) {
         dev_warn(dev->device, "DmaBuf_AddBuf: Non-contiguous memory is not supported\n");
         goto error;
      }
      addr = sg_page_iter_dma_address(&iter);
      size += PAGE_SIZE;
      nents++;
   }

   /* We cannot support buffers > 4GiB */
   if (size > 0xFFFFFFFFULL) {
      dev_warn(dev->device, "DmaBuf_AddBuf: DMA buffer too large: 0x%lX > 0xFFFFFFFF\n", size);
      ret = -EINVAL;
      goto error;
   }

   /* Lookup registers based on the async core version */
   uint32_t offset = 0x0;
   const struct GpuAsyncRegister *countReg = NULL, *enableReg = NULL;
   switch(rdmaData->version) {
   case 0 ... 3:
      offset = write ? GPU_ASYNC_REG_WRITE_ADDR_L_OFFSET_V1(*count) : GPU_ASYNC_REG_READ_ADDR_L_OFFSET_V1(*count);
      countReg = write ? &GpuAsyncReg_WriteCountV1 : &GpuAsyncReg_ReadCountV1;
      enableReg = write ? &GpuAsyncReg_WriteEnableV1 : &GpuAsyncReg_ReadEnableV1;
      break;
   default:
   case 4:
      offset = write ? GPU_ASYNC_REG_WRITE_ADDR_L_OFFSET_V4(*count) : GPU_ASYNC_REG_READ_ADDR_L_OFFSET_V4(*count);
      countReg = write ? &GpuAsyncReg_WriteCountV4 : &GpuAsyncReg_ReadCountV4;
      enableReg = write ? &GpuAsyncReg_WriteEnableV4 : &GpuAsyncReg_ReadEnableV4;
      break;
   }

   /* Write out DMA address */
   writel(phys_base & 0xFFFFFFFF, rdmaData->base + offset);
   writel(((uint64_t)phys_base >> 32ULL) & 0xFFFFFFFF, rdmaData->base + offset + 0x4);

   /* Write out max size */
   if (rdmaData->version >= 4) {
      if (write) {
         writeGpuAsyncReg(rdmaData->base, &GpuAsyncReg_RemoteWriteMaxSizeV4, size);
      }
   } else if (write) {
      writel(size, rdmaData->base + GPU_ASYNC_REG_WRITE_SIZE_OFFSET_V1(*count));
   }

   (*count)++;

   /* Set counts and enable */
   writeGpuAsyncReg(rdmaData->base, countReg, *count-1);
   writeGpuAsyncReg(rdmaData->base, enableReg, 1);

   buffer->dma_attach = attach;
   buffer->phys_base = phys_base;
   buffer->tab = tab;
   buffer->dma_buf = buf;

   //if (dev->debug > 0)
      dev_info(dev->device, "DmaBuf_AddBuf: Added DMA buffer %d: phys_addr=0x%llX, len=0x%lX\n", *count-1, phys_base, size);

   mutex_unlock(&info->lock);
   return 0;
error:
   if (!IS_ERR(tab))
      dma_buf_unmap_attachment(attach, tab, write ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
   if (!IS_ERR(attach) && !IS_ERR(buf))
      dma_buf_detach(buf, attach);
   if (!IS_ERR(buf))
      dma_buf_put(buf);
   mutex_unlock(&info->lock);
   return ret;
}

static int DmaBuf_RemoveBuf(struct DmaDevice* dev, struct DmaBufBuffer* buffer) {
   if (!buffer || !buffer->dma_buf)
      return 0; /* Nothing to do */

   if (dev->debug > 0)
      dev_info(dev->device, "DmaBuf_RemoveBuf: Releasing buffer phys_base=0x%llX\n", buffer->phys_base);

   dma_buf_unmap_attachment(buffer->dma_attach, buffer->tab, buffer->dir);
   dma_buf_put(buffer->dma_buf);
   memset(buffer, 0, sizeof(*buffer));
   return 0;
}

/**
 * We don't really have a good way to remove individual buffers, so they have to be removed in bulk. When designing V4 of
 * GpuAsyncCore, we assumed that all read or write buffers would be identically sized (hence RemoteWriteMaxSize). GpuAsyncCore
 * will also round-robin buffers and doesn't have a proper "free list", thus buffer descriptions must be contiguous in the register
 * space (we cannot have holes).
 * In theory, we could shuffle around buffer descriptions, however this would introduce some overhead as we need to disable DMAs,
 * wait for them to be disabled, shuffle, re-enable.
 */
static int DmaBuf_RemoveBuffers(struct DmaDevice* dev) {
   struct DmaBufData* info = DmaBuf_GetPvt(dev);
   if (!info) {
      return -ENOTSUPP;
   }
   
   struct RdmaData* rdmaData = dev->rdmaData;

   /* Clear FPGA state */
   Rdma_ClearBufferRegs(dev);

   /* Release each buffer now */
   for (int i = 0; i < rdmaData->readBufferCount; ++i)
      DmaBuf_RemoveBuf(dev, &info->readBuffs[i]);

   for (int i = 0; i < rdmaData->writeBufferCount; ++i)
      DmaBuf_RemoveBuf(dev, &info->writeBuffs[i]);

   return 0;
}

int DmaBuf_Ioctl(struct DmaDevice* dev, uint32_t cmd, uint64_t arg0) {
   if (!DmaBuf_GetPvt(dev)) {
      return -ENOTSUPP;
   }

   switch (cmd) {
      case GPU_DmaBuf_Add_Wr_Buffer:
         return DmaBuf_AddBuf(dev, (int)arg0, 1);
      case GPU_DmaBuf_Add_Rd_Buffer:
         return DmaBuf_AddBuf(dev, (int)arg0, 0);
      case GPU_DmaBuf_Remove_Buffers:
         return DmaBuf_RemoveBuffers(dev);
      default:
         break;
   }
   return -EINVAL;
}