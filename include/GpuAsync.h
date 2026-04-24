/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    Provides definitions and inline functions for utilizing GPU asynchronous
 *    features within the aes_stream_drivers package.
 *
 *    This code is specifically designed for managing NVIDIA GPU memory in a
 *    Linux kernel module, offering functionality to add and remove memory
 *    regions for GPU access.
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

#include "DmaDriver.h"

/**
 * GPU command codes
 **/
#define GPU_Add_Nvidia_Memory 0x8002   // Command to add NVIDIA GPU memory
#define GPU_Rem_Nvidia_Memory 0x8003   // Command to remove NVIDIA GPU memory
#define GPU_Set_Write_Enable  0x8004   // Set Write Enable Flag
#define GPU_Is_Gpu_Async_Supp 0x8005   // Check if GPU Async is supported by firmware
#define GPU_Get_Gpu_Async_Ver 0x8006   // Get the GpuAsyncCore version
#define GPU_Get_Max_Buffers   0x8007   // Get the max number of DMA buffers

/**
 * @brief Represents NVIDIA GPU memory data.
 *
 * This structure is used for managing memory regions in NVIDIA GPUs,
 * specifically for adding or removing access to these regions.
 **/
struct GpuNvidiaData {
   uint32_t write;    /**< Write permission flag (non-zero for write access). */
   uint64_t address;  /**< GPU memory address. */
   uint32_t size;     /**< Size of the memory region in bytes. */
};

#ifndef DMA_IN_KERNEL

/**
 * @brief Adds a NVIDIA GPU memory region.
 *
 * This function adds a specified memory region to the NVIDIA GPU, allowing
 * for the region to be accessed as specified by the write flag.
 *
 * @param fd       File descriptor for the device.
 * @param write    Write access flag (1 for write access, 0 for read-only).
 * @param address  Memory address of the GPU region to add.
 * @param size     Size of the memory region to add.  Must be a multiple of 64 KB.
 *
 * @return On success, returns the result of the ioctl call.  On failure,
 *         returns a negative error code.  Returns -ENOTSUPP if the firmware
 *         does not support GPUDirect.
 **/
static inline ssize_t gpuAddNvidiaMemory(int32_t fd, uint32_t write, uint64_t address, uint32_t size) {
   struct GpuNvidiaData dat;

   dat.write = write;
   dat.address = address;
   dat.size = size;

   return(ioctl(fd, GPU_Add_Nvidia_Memory, &dat));
}

/**
 * @brief Removes a NVIDIA GPU memory region.
 *
 * This function removes a previously added memory region from the NVIDIA GPU,
 * ceasing its accessibility.
 *
 * @param fd File descriptor for the device.
 *
 * @return On success, returns the result of the ioctl call.  On failure,
 *         returns a negative error code.  Returns -ENOTSUPP if the firmware
 *         does not support GPUDirect.
 **/
static inline ssize_t gpuRemNvidiaMemory(int32_t fd) {
   return(ioctl(fd, GPU_Rem_Nvidia_Memory, 0));
}

/**
 * @brief Set write enable for buffer.
 *
 * This function enables a DMA buffer for DMA operations.
 *
 * @param fd  File descriptor for the device.
 * @param idx Buffer index to enable.
 *
 * @return 0 on success, negative error code on failure.  Returns -ENOTSUPP
 *         if the firmware does not support GPUDirect.
 */
static inline ssize_t gpuSetWriteEn(int32_t fd, uint32_t idx) {
   uint32_t lidx = idx;
   return(ioctl(fd, GPU_Set_Write_Enable, &lidx));
}

/**
 * @brief Check if the firmware supports GPU Async.
 *
 * @param fd File descriptor for the device.
 *
 * @return @c true if the firmware and driver support GPU Async, @c false
 *         otherwise (including when the driver was built without GPUAsync
 *         support and returns @c -ENOTSUPP, or when the ioctl itself fails
 *         with @c -1).
 *
 * @note The ioctl returns @c 1 (supported), @c 0 (not supported), or a
 *       negative errno on failure.  We must compare against @c >0 because
 *       casting @c -1 directly to @c bool yields @c true and would produce
 *       a false-positive "supported" result on driver/fd errors.
 */
static inline bool gpuIsGpuAsyncSupported(int32_t fd) {
   return ioctl(fd, GPU_Is_Gpu_Async_Supp) > 0;
}

/**
 * @brief Get the version of GpuAsyncCore in the firmware.
 *
 * @param fd File descriptor for the device.
 *
 * @return The version of GpuAsyncCore, or 0 if not supported.  Returns
 *         -ENOTSUPP if the driver was compiled without GPUAsync support.
 *
 * @note The return type is @c uint32_t to preserve the legacy public ABI
 *       of this header.  The underlying ioctl() may return a negative
 *       errno (e.g. @c -ENOTSUPP); callers that need to distinguish an
 *       error from a valid version must cast the return to @c int32_t
 *       before comparison.  Do not change the return type: downstream
 *       consumers build against this signature.
 */
static inline uint32_t gpuGetGpuAsyncVersion(int32_t fd) {
   return ioctl(fd, GPU_Get_Gpu_Async_Ver);
}

/**
 * @brief Get the maximum number of DMA buffers.
 *
 * @param fd File descriptor for the device.
 *
 * @return The number of DMA buffers available for use.  Returns -ENOTSUPP
 *         if the driver was compiled without GPUAsync support.
 *
 * @note Same signed/unsigned caveat as gpuGetGpuAsyncVersion() — the
 *       @c uint32_t return is a legacy ABI constraint; cast to
 *       @c int32_t at the call site to detect negative errnos.
 */
static inline uint32_t gpuGetMaxBuffers(int32_t fd) {
   return ioctl(fd, GPU_Get_Max_Buffers);
}

#endif  // !DMA_IN_KERNEL
#endif  // __GPU_ASYNC_H__
