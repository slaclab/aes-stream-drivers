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
#include <rdma_common.h>
#include <rdma_dma_buf.h>
#include <rdma_nv_p2p.h>
#include <GpuAsyncRegs.h>
#include <GpuAsync.h>

#include <linux/seq_file.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/delay.h>

/* Update this when you add support for a new GpuAsyncCore version! */
#define DATAGPU_MAX_VERSION 5

/**
 * Rdma_Init - Initialize GPU with given offset
 * @dev: pointer to the DmaDevice structure
 * @offset: memory offset for GPU initialization
 *
 * This function allocates memory for GpuData structure, initializes
 * it, and associates it with the given DmaDevice. It sets up
 * the base address for GPU operations and initializes buffer counts.
 */
int32_t Rdma_Init(struct DmaDevice *dev, uint32_t offset) {
   struct RdmaData* rdmaData;

   uint32_t maxBuffers = 0;
   uint8_t* gpuBase = dev->base + offset;
   uint8_t version = readGpuAsyncReg(gpuBase, &GpuAsyncReg_Version);
   dev->gpuEn = !!version;
   dev->gpuVer = version;

   // GPU not enabled, avoid allocating GPU data */
   if (!dev->gpuEn)
      return 0;

   // warn on unsupported version
   if (version > DATAGPU_MAX_VERSION) {
      dev_err(dev->device, "Rdma_Init: Unsupported GpuAsyncCore version: %d. Max supported is version %d\n",
            version, DATAGPU_MAX_VERSION);
      dev->gpuEn = 0;
      return 0;  // allow fallback to CPU DMA
   }

   // Read the firmware buffer count before allocating any GPU state
   if (version < 4) {
      maxBuffers = readGpuAsyncReg(gpuBase, &GpuAsyncReg_MaxBuffersV1);
   } else {
      maxBuffers = readGpuAsyncReg(gpuBase, &GpuAsyncReg_MaxBuffersV4);
   }

   /* The writeBuffers/readBuffers list[] arrays are statically sized to
    * MAX_GPU_BUFFERS. If firmware reports more than we can hold, Gpu_AddNvidia
    * would index past the array and corrupt kernel memory. Refuse to enable
    * GPU in that case; the device still works via the CPU DMA path. */
   if (maxBuffers > MAX_GPU_BUFFERS) {
      dev_err(dev->device, "Rdma_Init: Firmware reports unsupported buffer count: %u > %d\n",
         maxBuffers, MAX_GPU_BUFFERS);
      dev->gpuEn = 0;
      return 0;  /* allow fallback to CPU DMA */
   }

   // Allocate memory for GPU utility data
   rdmaData = (struct RdmaData *)kzalloc(sizeof(struct RdmaData), GFP_KERNEL);
   if (!rdmaData) {
      dev_err(dev->device, "Rdma_Init: Failed to allocate RdmaData space of size %ld bytes\n",
         (ulong)(sizeof(struct RdmaData)));
      return -ENOMEM;  /* allocation failure aborts probe */
   }

   // Associate GPU utility data with the device
   dev->rdmaData = rdmaData;

   // Initialize GPU base address and buffer counts
   rdmaData->base = dev->base + offset;
   rdmaData->writeBufferCount = 0;
   rdmaData->readBufferCount = 0;
   rdmaData->offset = offset;
   rdmaData->version = version;
   rdmaData->maxBuffers = maxBuffers;
   mutex_init(&rdmaData->lock);
   atomic64_set(&rdmaData->pid, 0);

   dev_info(dev->device, "Rdma_Init: Configured for GpuAsyncCore version %d\n", version);

#ifdef HAVE_NV_P2P
   // Init nvidia p2p component (if available)
   rdmaData->nvP2PData = NvP2P_Init(dev, offset);
   if (!rdmaData->nvP2PData) {
      dev_err(dev->device, "Rdma_Init: Failed to init NVIDIA p2p support\n");
      kfree(rdmaData);
      return -EFAULT;
   }
#endif

   // Init dma-buf component
   rdmaData->dmaBufData = DmaBuf_Init(dev, offset);
   if (!rdmaData->dmaBufData) {
      dev_err(dev->device, "Rdma_Init: Failed to init dma-buf support\n");
      // Just eat the error..
      
      dev_info(dev->device, "Rdma_Init: Supported APIs: nvidia-p2p\n");
   } else {
      dev_info(dev->device, "Rdma_Init: Supported APIs: nvidia-p2p, dma-buf\n");
   }

   return 0;
}

void Rdma_Shutdown(struct DmaDevice* dev) {
   struct RdmaData* data = dev->rdmaData;
   if (!data)
      return;

#ifdef HAVE_NV_P2P
   if (data->nvP2PData) {
      NvP2P_Shutdown(dev);
   }
#endif

   if (data->dmaBufData) {
      DmaBuf_Shutdown(dev);
   }
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
int32_t Rdma_Ioctl(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   struct RdmaData* data = dev->rdmaData;
   if (!data) {
      // This should be supported regardless of dev->rdmaData
      if (cmd == GPU_Is_Gpu_Async_Supp)
         return 0;
      return -ENOTSUPP;
   }

   switch (cmd) {
      // Set write enable flag
      case GPU_Set_Write_Enable:
         return Rdma_SetWriteEn(dev, arg);

      // Get the max number of buffers
      case GPU_Get_Max_Buffers:
         return (int32_t)data->maxBuffers;

      // Enable TX operations
      case GPU_Enable_Tx:
         return Rdma_EnableTx(dev, arg);

      // Enable RX operations
      case GPU_Enable_Rx:
         return Rdma_EnableRx(dev, arg);

      // NVIDIA p2p IOCTLs
      case GPU_Add_Nvidia_Memory:
      case GPU_Rem_Nvidia_Memory:
      #ifdef HAVE_NV_P2P
         return NvP2P_Ioctl(dev, cmd, arg);
      #else
         return -ENOTSUPP;
      #endif // HAVE_NV_P2P

      case GPU_DmaBuf_Add_Wr_Buffer:
      case GPU_DmaBuf_Add_Rd_Buffer:
      case GPU_DmaBuf_Remove_Buffers:
         return DmaBuf_Ioctl(dev, cmd, arg);

      case GPU_Get_Gpu_Async_Ver:
         return data->version;

      case GPU_Is_Gpu_Async_Supp:
         return data->version > 0;

      case GPU_Is_Dma_Buf_Supp:
         return !!data->dmaBufData;

      case GPU_Is_GpuDirect_Supp:
         return !!data->nvP2PData;

      default:
         dev_warn(dev->device, "Command: Invalid command=%u\n", cmd);
         return -1;
   }
}

/**
 * Rdma_SetWriteEn - Set write enable for buffer
 * @dev: pointer to the DMA device structure
 * @arg: user space argument pointing to buffer index
 *
 * This function enables a DMA buffer for DMA operations.
 *
 * Return: 0 on success, negative error code on failure.
 */
int32_t Rdma_SetWriteEn(struct DmaDevice *dev, uint64_t arg) {
   uint32_t idx;
   uint32_t ret;
   uint32_t offset = 0;

   struct RdmaData* data = dev->rdmaData;

   // Check for calling process ownership. Unlocked GpuAsyncCore is OK
   pid_t pid = atomic64_read(&data->pid);
   if (pid && pid != current->pid) {
      dev_warn(dev->device, "Rdma_SetWriteEn: Called by non-owner PID (%d)\n",
               current->pid);
      return -EBUSY;
   }

   // Copy data from user space
   if ((ret = copy_from_user(&idx, (void *)arg, sizeof(uint32_t)))) {
      dev_warn(dev->device, "Rdma_SetWriteEn: copy_from_user failed. ret=%i, user=%p\n", ret, (void *)arg);
      return -EINVAL;
   }

   if ( idx >= data->writeBufferCount ) {
      dev_warn(dev->device, "Rdma_SetWriteEn: Invalid write buffer index idx=%i, count=%i\n", idx, data->writeBufferCount);
      return -EINVAL;
   }

   if (data->version < 4) {
      offset = GPU_ASYNC_REG_WRITE_DETECT_BASE_V1 + idx * 4;
   } else {
      offset = GPU_ASYNC_REG_WRITE_DETECT_BASE_V4 + idx * 4;
   }

   writel(0x1, data->base + offset);

   return 0;
}

/**
 * Rdma_ClearBufferRegs - Clear register state on the FPGA
 * Safe to call multiple times.
 *
 * @dev: The underlying DMA device to free the buffers on
 */
void Rdma_ClearBufferRegs(struct DmaDevice* dev) {
   uint32_t x;
   u64 offset;
   ktime_t waitStart;

   struct RdmaData *data;

   // Retrieve the GPU specific data from the DMA device
   data = (struct RdmaData *)dev->rdmaData;

   // Skip this code if this has already been disabled.
   if (data->disabled)
      return;

   // Disable reads and writes before freeing underlying buffers.
   writel(0, data->base + 0x008);

   // GpuAsyncV4 has no "DMA complete" indicator, so we need to delay for a bit while pending transactions complete.
   // This is far from scientific; I'm just choosing a value (50ms) that *should* prevent crashes...
   if (data->version < 5) {
      if (!data->disabled)
         fsleep(50000);
      data->disabled = 1;
   } else {
      // V5+: Spin on write/read enable readback. This will get cleared once the FPGA has completed all RDMA transactions to the GPU.
      waitStart = ktime_get();
      while (readl(data->base + 0x44) != 0) {
         cpu_relax();

         // Avoid hanging the system if there's a stalled transfer
         if (ktime_to_ms(ktime_sub(ktime_get(), waitStart)) > 1000) {
            dev_warn(dev->device, "Rdma_ClearBufferRegs: Possible stalled DMA; already waited for 1s\n");
            break;
         }
      }
      data->disabled = 1;
   }

   // Clear out remote write size register
   if (data->version >= 4) {
      writeGpuAsyncReg(data->base, &GpuAsyncReg_RemoteWriteMaxSizeV4, 0);
   }

   // Clear out the write buffer registers
   for (x = 0; x < data->writeBufferCount; x++) {
      //buffer = &(data->writeBuffers.list[x]);

      // Compute version specific offsets
      if (data->version < 4) {
         offset = GPU_ASYNC_REG_WRITE_BASE_V1 + x * 16;
      } else {
         offset = GPU_ASYNC_REG_WRITE_BASE_V4 + x * 8;
      }

      // Clear address register; firmware may initiate an rdma transaction even when dropEn=1, which
      // typically leads to a hang in the GPU software under some circumstances. This is usually a problem
      // when 2 or more FPGAs end up with the same physical addresses in these registers (e.g. if you're running
      // an application against multiple different FPGAs and one GPU)
      writel(0, data->base + offset);
      writel(0, data->base + offset + 0x4);
   }

   // Clear out the read buffer registers
   for (x = 0; x < data->readBufferCount; x++) {
      //buffer = &(data->readBuffers.list[x]);

      // Compute version specific offsets
      if (data->version < 4) {
         offset = GPU_ASYNC_REG_READ_BASE_V1 + data->readBufferCount * 16;
      } else {
         offset = GPU_ASYNC_REG_READ_BASE_V4 + data->readBufferCount * 8;
      }

      // See comment in the previous for loop for why this is done.
      writel(0, data->base + offset);
      writel(0, data->base + offset + 0x4);
   }

   dev_info(dev->device, "Rdma_ClearBufferRegs: Unmapped %d write buffers and %d read buffers\n", data->writeBufferCount, data->readBufferCount);

   // Reset the buffer counts
   data->writeBufferCount = 0;
   data->readBufferCount = 0;

   // Release GpuAsyncCore to other processes
   atomic64_set(&data->pid, 0);
}

/**
 * Rdma_Show - Show information about DataGpu internal state
 * @s: Sequence file pointer to write to
 * @dev: Device to read from
 */
void Rdma_Show(struct seq_file *s, struct DmaDevice *dev) {
   u32 i;
   struct RdmaData* data = dev->rdmaData;
   if (unlikely(!data)) {
      BUG();
      return;
   }

   u32 readBuffCnt = 0;
   u32 writeBuffCnt = 0;
   u32 writeEnable = 0;
   u32 readEnable = 0;
   u32 maxBuffers = 0;

   if (data->version < 4) {
      readBuffCnt = readGpuAsyncReg(data->base, &GpuAsyncReg_ReadCountV1)+1;
      writeBuffCnt = readGpuAsyncReg(data->base, &GpuAsyncReg_WriteCountV1)+1;
      writeEnable = readGpuAsyncReg(data->base, &GpuAsyncReg_WriteEnableV1);
      readEnable = readGpuAsyncReg(data->base, &GpuAsyncReg_ReadEnableV1);
      maxBuffers = readGpuAsyncReg(data->base, &GpuAsyncReg_MaxBuffersV1);
   } else {
      readBuffCnt = readGpuAsyncReg(data->base, &GpuAsyncReg_ReadCountV4)+1;
      writeBuffCnt = readGpuAsyncReg(data->base, &GpuAsyncReg_WriteCountV4)+1;
      writeEnable = readGpuAsyncReg(data->base, &GpuAsyncReg_WriteEnableV4);
      readEnable = readGpuAsyncReg(data->base, &GpuAsyncReg_ReadEnableV4);
      maxBuffers = readGpuAsyncReg(data->base, &GpuAsyncReg_MaxBuffersV4);
   }

   seq_printf(s, "\n---------------- DataGPU State ----------------\n");
   seq_printf(s, "    GpuAsyncCore Offset : 0x%X\n", data->offset);
   seq_printf(s, "   GpuAsyncCore Version : %d\n", data->version);
   seq_printf(s, "            Max Buffers : %u\n", maxBuffers);
   seq_printf(s, "     Write Buffer Count : %u\n", writeBuffCnt);
   seq_printf(s, "           Write Enable : %u\n", writeEnable);
   seq_printf(s, "      Read Buffer Count : %u\n", readBuffCnt);
   seq_printf(s, "            Read Enable : %u\n", readEnable);
   seq_printf(s, "         RX Frame Count : %u\n", readGpuAsyncReg(data->base, &GpuAsyncReg_RxFrameCnt));
   seq_printf(s, "         TX Frame Count : %u\n", readGpuAsyncReg(data->base, &GpuAsyncReg_TxFrameCnt));
   seq_printf(s, "  AXI Write Error Count : %u\n", readGpuAsyncReg(data->base, &GpuAsyncReg_AxiWriteErrorCnt));
   if (data->version >= 2)  // Added in V2
      seq_printf(s, "AXI Write Timeout Count : %u\n", readGpuAsyncReg(data->base, &GpuAsyncReg_AxiWriteTimeoutCnt));
   if (data->version >= 3) {  // Added in V3
      seq_printf(s, "      Min Write Buffers : %u\n", readGpuAsyncReg(data->base, &GpuAsyncReg_MinWriteBuffer));
      seq_printf(s, "       Min Read Buffers : %u\n", readGpuAsyncReg(data->base, &GpuAsyncReg_MinReadBuffer));
   }
   seq_printf(s, "   AXI Read Error Count : %u\n", readGpuAsyncReg(data->base, &GpuAsyncReg_AxiReadErrorCnt));
   seq_printf(s, "         Owning Process : %llu\n", (u64)atomic64_read(&data->pid));

   for (i = 0; i < writeBuffCnt && writeEnable; ++i) {
      u32 wal, wah, ws;
      if (data->version < 4) {
         wal = readl(data->base + GPU_ASYNC_REG_WRITE_ADDR_L_OFFSET_V1(i));
         wah = readl(data->base + GPU_ASYNC_REG_WRITE_ADDR_H_OFFSET_V1(i));
         ws = readl(data->base + GPU_ASYNC_REG_WRITE_SIZE_OFFSET_V1(i));
      } else {
         wal = readl(data->base + GPU_ASYNC_REG_WRITE_ADDR_L_OFFSET_V4(i));
         wah = readl(data->base + GPU_ASYNC_REG_WRITE_ADDR_H_OFFSET_V4(i));
         ws = readGpuAsyncReg(data->base, &GpuAsyncReg_RemoteWriteMaxSizeV4);
      }

      seq_printf(s, "\n-------- Write Buffer %u --------\n", i);
      seq_printf(s, "  Write Address : 0x%llX\n", ((u64)wah << 32) | wal);
      seq_printf(s, "     Write Size : 0x%X\n", ws);
   }

   for (i = 0; i < readBuffCnt && readEnable; ++i) {
      u32 ral, rah, rs;
      if (data->version < 4) {
         ral = readl(data->base + GPU_ASYNC_REG_READ_ADDR_L_OFFSET_V1(i));
         rah = readl(data->base + GPU_ASYNC_REG_READ_ADDR_H_OFFSET_V1(i));
         rs = readl(data->base + GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET_V1(i));
      } else {
         ral = readl(data->base + GPU_ASYNC_REG_READ_ADDR_L_OFFSET_V4(i));
         rah = readl(data->base + GPU_ASYNC_REG_READ_ADDR_H_OFFSET_V4(i));
         rs = readl(data->base + GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET_V4(i));
      }
      seq_printf(s, "\n-------- Read Buffer %u --------\n", i);
      seq_printf(s, "  Read Address : 0x%llX\n", ((u64)rah << 32) | ral);
      seq_printf(s, "     Read Size : 0x%X\n", rs);
   }
}

/**
 * @brief Toggles the write enable bit
 * @param dev The device
 * @param enable Enable or disable. Treated as a boolean.
 */
int32_t Rdma_EnableTx(struct DmaDevice *dev, uint64_t enable) {
   struct RdmaData* data = dev->rdmaData;

   // Check for calling process ownership. Unlocked GpuAsyncCore is OK
   pid_t pid = atomic64_read(&data->pid);
   if (pid && pid != current->pid) {
      dev_warn(dev->device, "Rdma_EnableTx: Called by non-owner PID (%d)\n",
               current->pid);
      return -EBUSY;
   }

   const struct GpuAsyncRegister* theReg = NULL;
   if (data->version < 4) {
      theReg = &GpuAsyncReg_WriteEnableV1;
   } else {
      theReg = &GpuAsyncReg_WriteEnableV4;
   }

   writeGpuAsyncReg(data->base, theReg, !!enable);
   return 0;
}

/**
 * @brief Toggles the read enable bit
 * @param dev The device
 * @param enable Enable or disable. Treated as a boolean.
 */
int32_t Rdma_EnableRx(struct DmaDevice *dev, uint64_t enable) {
   struct RdmaData* data = dev->rdmaData;

   // Check for calling process ownership. Unlocked GpuAsyncCore is OK
   pid_t pid = atomic64_read(&data->pid);
   if (pid && pid != current->pid) {
      dev_warn(dev->device, "Rdma_EnableRx: Called by non-owner PID (%d)\n",
               current->pid);
      return -EBUSY;
   }

   const struct GpuAsyncRegister* theReg = NULL;
   if (data->version < 4) {
      theReg = &GpuAsyncReg_ReadEnableV1;
   } else {
      theReg = &GpuAsyncReg_ReadEnableV4;
   }

   writeGpuAsyncReg(data->base, theReg, !!enable);
   return 0;
}
