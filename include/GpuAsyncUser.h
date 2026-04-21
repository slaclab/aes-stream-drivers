/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: User space API for Gpu Async support. Attempts to abstract away
 *  some of the internal implementation detail from user-space software.
 *  This file contains no handling for big-endian systems.
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
#ifndef _GPU_ASYNC_USER_H_
#define _GPU_ASYNC_USER_H_

#include <GpuAsyncRegs.h>

#if defined(__cplusplus) && __cplusplus < 201103L
#error The code in this file requires C++11
#endif

#ifdef __cplusplus

#include <cuda.h>
#include <cuda_runtime.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <new>

#include "DmaDriver.h"
#include "GpuAsync.h"

/**
 * @brief Thin wrapper around the C API and definitions in GpuAsyncRegs.h
 * The lifetime of this object must be within the lifetime of the memory mapped registers provided in the constructor.
 *
 * Code calling into this object to retrieve register values does not need to be aware of the offsets or GpuAsyncCore version.
 */
class GpuAsyncCoreRegs {
public:
   GpuAsyncCoreRegs() = delete;

   /**
    * @brief regs Pointer to the memory mapped GpuAsyncCore registers.
    */
   explicit GpuAsyncCoreRegs(volatile void* regs) :
      regs_((volatile uint8_t*)regs) {
      this->version_ = readReg(GpuAsyncReg_Version);
   }

   inline volatile uint8_t* registers() const {
      return (volatile uint8_t*)this->regs_;
   }

   inline uint32_t readReg(const GpuAsyncRegister& reg) const {
      return readGpuAsyncReg(regs_, &reg);
   }

   /**
    * @brief Read register at a specific offset, instead of using the GpuAsyncRegister struct
    */
   inline uint32_t readReg(uint32_t offset) const {
      return *(uint32_t*)(regs_ + offset);
   }

   inline void writeReg(const GpuAsyncRegister& reg, uint32_t value) {
      writeGpuAsyncReg(regs_, &reg, value);
   }

   inline void writeReg(uint32_t offset, uint32_t value) {
      *(uint32_t*)(regs_ + offset) = value;
   }

   /**
    * @brief Returns the version of GpuAsyncCore this firmware is running
    */
   inline uint32_t version() const { return version_; }

   /**
    * @brief Returns the max number of buffers supported by the firmware
    */
   uint32_t maxBuffers() const {
      return readRegV1V4(GpuAsyncReg_MaxBuffersV1, GpuAsyncReg_MaxBuffersV4);
   }

   uint32_t arCache() const {
      return readReg(GpuAsyncReg_ArCache);
   }

   uint32_t awCache() const {
      return readReg(GpuAsyncReg_AwCache);
   }

   /**
    * @brief Returns the number of dma header bytes, DMA_AXI_CONFIG_G.DATA_BYTES_C
    */
   uint32_t dmaDataBytes() const {
      return readReg(GpuAsyncReg_DmaDataBytes);
   }

   inline uint32_t writeCount() const {
      return readRegV1V4(GpuAsyncReg_WriteCountV1, GpuAsyncReg_WriteCountV4);
   }

   inline void setWriteCount(uint32_t val) {
      writeRegV1V4(GpuAsyncReg_WriteCountV1, GpuAsyncReg_WriteCountV4, val);
   }

   inline uint32_t writeEnable() const {
      return readRegV1V4(GpuAsyncReg_WriteEnableV1, GpuAsyncReg_WriteEnableV4);
   }

   inline void setWriteEnable(uint32_t val) {
      writeRegV1V4(GpuAsyncReg_WriteEnableV1, GpuAsyncReg_WriteEnableV4, val);
   }

   inline uint32_t readCount() const {
      return readRegV1V4(GpuAsyncReg_ReadCountV1, GpuAsyncReg_ReadCountV4);
   }

   inline void setReadCount(uint32_t val) {
      writeRegV1V4(GpuAsyncReg_ReadCountV1, GpuAsyncReg_ReadCountV4, val);
   }

   inline uint32_t readEnable() const {
      return readRegV1V4(GpuAsyncReg_ReadEnableV1, GpuAsyncReg_ReadEnableV4);
   }

   inline void setReadEnable(uint32_t val) {
      writeRegV1V4(GpuAsyncReg_ReadEnableV1, GpuAsyncReg_ReadEnableV4, val);
   }

   void countReset() {
      writeReg(GpuAsyncReg_CntRst, 1);
   }

   inline uint32_t rxFrameCount() const {
      return readReg(GpuAsyncReg_RxFrameCnt);
   }

   inline uint32_t txFrameCount() const {
      return readReg(GpuAsyncReg_TxFrameCnt);
   }

   inline uint32_t axiWriteErrorCount() const {
      return readReg(GpuAsyncReg_AxiWriteErrorCnt);
   }

   inline uint32_t axiReadErrorCount() const {
      return readReg(GpuAsyncReg_AxiReadErrorCnt);
   }

   inline uint32_t axiWriteErrorVal() const {
      return readReg(GpuAsyncReg_AxiWriteErrorVal);
   }

   inline uint32_t axiReadErrorVal() const {
      return readReg(GpuAsyncReg_AxiReadErrorVal);
   }

   inline uint32_t axiWriteTimeoutCount() const {
      return readReg(GpuAsyncReg_AxiWriteTimeoutCnt);
   }

   inline uint32_t axisDeMuxSelect() const {
      return readReg(GpuAsyncReg_AxisDeMuxSelect);
   }

   inline void setAxisDeMuxSelect(uint32_t val) {
      writeReg(GpuAsyncReg_AxisDeMuxSelect, val);
   }

   inline uint32_t minWriteBuffer() const {
      return readReg(GpuAsyncReg_MinWriteBuffer);
   }

   inline uint32_t minReadBuffer() const {
      return readReg(GpuAsyncReg_MinReadBuffer);
   }

   /**
    * @brief Returns the total round-trip latency, in clock cycles, reported for the buffer
    * @note For V4+, the buffer argument is ignored and should be 0.
    */
   inline uint32_t totalLatency(uint32_t buffer) const {
      switch (versionSwitch()) {
      case 0:
         return 0;
      case 1:
         return readReg(GPU_ASYNC_REG_LATENCY_TOTAL_OFFSET_V1(buffer));
      case 4:
      default:
         return readReg(GpuAsyncReg_TotLatencyV4);
      }
   }

   /**
    * @brief Returns the GPU processing latency, in clock cycles, reported for the buffer
    * @note For V4+, the buffer argument is ignored and should be 0.
    */
   inline uint32_t gpuLatency(uint32_t buffer) const {
      switch (versionSwitch()) {
      case 0:
         return 0;
      case 1:
         return readReg(GPU_ASYNC_REG_LATENCY_GPU_OFFSET_V1(buffer));
      case 4:
      default:
         return readReg(GpuAsyncReg_GpuLatencyV4);
      }
   }

   /**
    * @brief Returns the FPGA -> GPU write latency, in clock cycles, reported for the buffer
    * @note For V4+, the buffer argument is ignored and should be 0.
    */
   inline uint32_t writeLatency(uint32_t buffer) const {
      switch (versionSwitch()) {
      case 0:
         return 0;
      case 1:
         return readReg(GPU_ASYNC_REG_LATENCY_WRITE_OFFSET_V1(buffer));
      case 4:
      default:
         return readReg(GpuAsyncReg_WrLatencyV4);
      }
   }

   /**
    * @brief Gets the remote write max size, used for FPGA -> GPU transfers
    * @param buffer The buffer to set the remote size for. Ignored in version >= 4
    * @note buffer is ignored when version() >= 4, since in V4 all buffers share the same register
    */
   inline uint32_t remoteWriteSize(uint32_t buffer) const {
      switch (versionSwitch()) {
      case 0:
         return 0;
      case 1:
         return readReg(GPU_ASYNC_REG_WRITE_SIZE_OFFSET_V1(buffer));
      case 4:
      default:
         return readReg(GpuAsyncReg_RemoteWriteMaxSizeV4);
      }
   }

   /**
    * @brief Sets the remote write max size, used for FPGA -> GPU transfers
    * @param buffer The buffer to set the remote size for. Ignored in version >= 4
    * @param size The size
    * @note buffer is ignored when version() >= 4, since in V4 all buffers share the same register
    */
   inline void setRemoteWriteMaxSize(uint32_t buffer, uint32_t size) {
      switch (versionSwitch()) {
      case 0:
         return;
      case 1:
         writeReg(GPU_ASYNC_REG_WRITE_SIZE_OFFSET_V1(buffer), size);
         return;
      case 4:
      default:
         writeReg(GpuAsyncReg_RemoteWriteMaxSizeV4, size);
         return;
      }
   }

   /**
    * @brief Sets the remote write address for the specified buffer. Used for FPGA -> GPU transfers
    * @param buffer The buffer index. Must be < 16 for V1, and < 1024 for V4
    * @param addr 64-bit address in GPU device memory
    */
   inline void setRemoteWriteAddress(uint32_t buffer, uint64_t addr) {
      uint32_t l = uint32_t(addr & 0xFFFFFFFF);
      uint32_t h = uint32_t((addr >> 32) & 0xFFFFFFFF);

      switch (versionSwitch()) {
      case 0:
         return;
      case 1:
         writeReg(GPU_ASYNC_REG_WRITE_ADDR_L_OFFSET_V1(buffer), l);
         writeReg(GPU_ASYNC_REG_WRITE_ADDR_H_OFFSET_V1(buffer), h);
         break;
      case 4:
      default:
         writeReg(GPU_ASYNC_REG_WRITE_ADDR_L_OFFSET_V4(buffer), l);
         writeReg(GPU_ASYNC_REG_WRITE_ADDR_H_OFFSET_V4(buffer), h);
         break;
      }
   }

   /**
    * @brief Sets the remote read address for the specified buffer. Used for GPU -> FPGA transfers
    * @param buffer The buffer index. Must be < 16 for V1, and < 1024 for V4
    * @param addr 64-bit address in GPU device memory
    */
   inline void setRemoteReadAddress(uint32_t buffer, uint64_t addr) {
      uint32_t l = uint32_t(addr & 0xFFFFFFFF);
      uint32_t h = uint32_t((addr >> 32) & 0xFFFFFFFF);

      switch (versionSwitch()) {
      case 0:
         return;
      case 1:
         writeReg(GPU_ASYNC_REG_READ_ADDR_L_OFFSET_V1(buffer), l);
         writeReg(GPU_ASYNC_REG_READ_ADDR_H_OFFSET_V1(buffer), h);
         break;
      case 4:
      default:
         writeReg(GPU_ASYNC_REG_READ_ADDR_L_OFFSET_V4(buffer), l);
         writeReg(GPU_ASYNC_REG_READ_ADDR_H_OFFSET_V4(buffer), h);
         break;
      }
   }

   /**
    * @brief Arms free list buffer for remote write from FPGA -> GPU.
    * @see triggerRemoteWriteOffset() for something usable with CUDA
    * @param buffer Buffer index to trigger.
    */
   inline void returnFreeListIndex(uint32_t buffer) {
      writeReg(freeListOffset(buffer), 1);
   }

   /**
    * @brief Returns the offset of the free list register from the start of the GpuAsyncCore registers
    * @param buffer The buffer index.
    */
   inline uint32_t freeListOffset(uint32_t buffer) const {
      switch (versionSwitch()) {
      case 0:  // Leaving this to return same as V1 for now
      case 1:
         return GPU_ASYNC_REG_WRITE_DETECT_OFFSET_V1(buffer);
      case 4:
      default:
         return GPU_ASYNC_REG_WRITE_DETECT_OFFSET_V4(buffer);
      }
   }

   /**
    * @brief Returns the offset of the remote read size register from the start of the GpuAsyncCore registers.
    * This is usable in CUDA kernels.
    * @param buffer the buffer index.
    */
   inline uint32_t remoteReadSizeOffset(uint32_t buffer) const {
      switch (versionSwitch()) {
      case 0:  // Leaving this to return same as V1 for now
      case 1:
         return GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET_V1(buffer);
      case 4:
      default:
         return GPU_ASYNC_REG_REMOTE_READ_SIZE_OFFSET_V4(buffer);
      }
   }

   /**
    * @brief Get the remote read size for the specified buffer
    */
   inline uint32_t remoteReadSize(uint32_t buffer) const {
      return readReg(remoteReadSizeOffset(buffer));
   }

   /**
    * @brief Set the remote read size for the buffer
    * @param buffer Buffer index
    * @param size Size of the GPU -> FPGA transfer
    */
   inline void setRemoteReadSize(uint32_t buffer, uint32_t size) {
      return writeReg(remoteReadSizeOffset(buffer), size);
   }

protected:
   // Squash version into [0, 1, 4] to make switches cleaner
   inline uint32_t versionSwitch() const {
      switch (version_) {
      case 0:
         return 0;
      case 1:
      case 2:
      case 3:
         return 1;
      case 4:
      default:
         return 4;
      }
   }

   uint32_t readRegV1V4(const GpuAsyncRegister& v1, const GpuAsyncRegister& v4) const {
      switch (versionSwitch()) {
      case 0:
         return 0;  // Unsupported
      case 1:
         return readReg(v1);
      case 4:
      default:
         return readReg(v4);
      }
   }

   void writeRegV1V4(const GpuAsyncRegister& v1, const GpuAsyncRegister& v4, uint32_t val) {
      switch (versionSwitch()) {
      case 0:
         return;  // Unsupported
      case 1:
         return writeReg(v1, val);
      case 4:
      default:
         return writeReg(v4, val);
      }
   }

   volatile uint8_t* regs_;
   uint32_t version_;
};

/**
 * @brief Context struct holding all GPU/FPGA state for a single GPUDirect RDMA session.
 *
 * @details Populated by gpuAsyncInit() and consumed by gpuAsyncCleanup().
 * gpuAsyncCleanup() releases every resource and resets the handles it owns
 * (fd, cuCtx, cuStream, regs, devRegs, rxBuffers, txBuffers) to NULL/-1/0,
 * but does not reset scalar configuration fields (bufCnt, bufSize,
 * dmaHeaderSize, loopback) or coreRegsStorage. The caller must not access
 * fields after calling gpuAsyncCleanup() and must memset() the struct to
 * zero before reusing it with another gpuAsyncInit() call.
 */
struct GpuAsyncContext {
   /** @brief File descriptor for the opened DMA device (e.g. /dev/datadev_0). */
   int32_t     fd;
   /** @brief CUDA context bound to the selected GPU device. */
   CUcontext   cuCtx;
   /** @brief CUDA stream used for asynchronous GPU operations. */
   CUstream    cuStream;
   /** @brief Aligned storage for in-place construction of GpuAsyncCoreRegs. */
   alignas(alignof(GpuAsyncCoreRegs)) uint8_t coreRegsStorage[sizeof(GpuAsyncCoreRegs)];
   /** @brief Array of GPU-allocated receive buffer pointers (one per DMA buffer). */
   uint8_t**   rxBuffers;
   /** @brief Array of GPU-allocated transmit buffer pointers (one per DMA buffer). */
   uint8_t**   txBuffers;
   /** @brief Number of DMA buffers allocated for rx and tx. */
   int32_t     bufCnt;
   /** @brief Size of each DMA buffer in bytes (must be a multiple of 64 KiB). */
   int32_t     bufSize;
   /** @brief DMA header size reported by the firmware (in bytes). */
   uint32_t    dmaHeaderSize;
   /** @brief Loopback mode flag: non-zero enables FPGA read-enable for loopback. */
   int32_t     loopback;
   /** @brief CUDA device pointer to the mapped GpuAsyncCore register block. */
   CUdeviceptr devRegs;
   /** @brief Host-mapped pointer to the GpuAsyncCore register block (via dmaMapRegister). */
   void*       regs;
};

/**
 * @brief Returns a pointer to the GpuAsyncCoreRegs object stored inside the context.
 * @param ctx Pointer to an initialised GpuAsyncContext.
 * @return Pointer to the placement-new'd GpuAsyncCoreRegs instance.
 */
static inline GpuAsyncCoreRegs* gpuAsyncRegs(GpuAsyncContext* ctx) {
   return reinterpret_cast<GpuAsyncCoreRegs*>(ctx->coreRegsStorage);
}

/**
 * @brief Initialise a GPU/FPGA session: opens the DMA device, creates a CUDA
 *        context, maps registers, allocates buffers, and enables DMA engines.
 *
 * @details Performs a 22-step ordered bring-up sequence: buffer-size validation,
 * CUDA device selection, stream-memops capability check, DMA device open,
 * GPU-async firmware support check, V4 firmware enforcement, buffer-count
 * validation against firmware maxBuffers, CUDA context creation, register
 * mapping via dmaMapRegister, CUDA host registration, GpuAsyncCoreRegs
 * placement-new, write/read engine disable (idempotent recovery from a
 * prior enabled state), header-size / devRegs computation, cudaMalloc of
 * rx/tx buffers, gpuAddNvidiaMemory for each buffer, CUDA stream creation,
 * free-list arming, doorbell clearing, stream-flush barrier before host
 * MMIO enable, and write/read count and enable setup.
 *
 * On failure the function unwinds exactly the steps that succeeded (goto
 * chain) so no resources are leaked.
 *
 * @param ctx      Pointer to a caller-allocated GpuAsyncContext (zeroed on entry).
 * @param dev      Device path string (e.g. "/dev/datadev_0").
 * @param gpuIdx   CUDA device index (passed to cuDeviceGet).
 * @param bufCnt   Number of DMA buffers to allocate (must be <= firmware maxBuffers).
 * @param bufSize  Size of each buffer in bytes (must be a positive multiple of 64 KiB).
 * @param loopback Non-zero to enable FPGA read-enable for loopback mode.
 *
 * @return 0 on success, -1 on failure (diagnostic printed to stderr).
 *
 * @note The caller must call cuInit(0) before calling gpuAsyncInit.
 */
static inline int gpuAsyncInit(GpuAsyncContext* ctx, const char* dev,
                               int gpuIdx, int bufCnt, int bufSize,
                               int loopback) {
   // Step 0: Establish the fd = -1 sentinel up front so that a pre-Step-4
   // failure (or a gpuAsyncCleanup() on a memset-zero context) does not
   // fall into the fd >= 0 guard and close stdin. This also lets callers
   // follow the documented "zeroed on entry" contract without having to
   // patch fd = -1 themselves after memset.
   ctx->fd = -1;

   // Step 1: bufSize validation — must be positive and a multiple of 64 KiB
   if (bufSize <= 0) {
      fprintf(stderr, "bufSize must be positive\n");
      return -1;
   }
   if (bufSize % 0x10000 != 0) {
      fprintf(stderr, "bufSize 0x%x is not a multiple of 64 KiB\n",
              (unsigned int)bufSize);
      return -1;
   }

   // Step 2: Get the CUDA device handle
   CUdevice computeDevice;
   {
      CUresult cr = cuDeviceGet(&computeDevice, gpuIdx);
      if (cr != CUDA_SUCCESS) {
         const char *en = NULL, *es = NULL;
         cuGetErrorName(cr, &en);
         cuGetErrorString(cr, &es);
         fprintf(stderr, "cuDeviceGet(%d) failed: %s (%s)\n",
                 gpuIdx, en ? en : "?", es ? es : "?");
         return -1;
      }
   }

   // Step 3: Verify stream memory ops are available on this device
   int pi = 0;  // default: not supported
   {
      CUresult cr = cuDeviceGetAttribute(
         &pi, CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_MEM_OPS_V1, computeDevice);
      if (cr != CUDA_SUCCESS) {
         const char *en = NULL, *es = NULL;
         cuGetErrorName(cr, &en);
         cuGetErrorString(cr, &es);
         fprintf(stderr, "cuDeviceGetAttribute(STREAM_MEM_OPS) failed on device %d: %s (%s)\n",
                 gpuIdx, en ? en : "?", es ? es : "?");
         return -1;
      }
      if (pi != 1) {
         fprintf(stderr, "Device %d cannot use Stream mem ops!\n", gpuIdx);
         return -1;
      }
   }

   // Step 4: Open the DMA device
   ctx->fd = open(dev, O_RDWR);
   if (ctx->fd < 0) {
      fprintf(stderr, "Failed to open %s: %s (errno=%d)\n",
              dev, strerror(errno), errno);
      return -1;
   }

   // Step 5: Check GPU async support
   if (!gpuIsGpuAsyncSupported(ctx->fd)) {
      fprintf(stderr, "Firmware or driver does not support GPUAsync\n");
      goto fail_fd;
   }

   // Step 6: V4 enforcement — reject firmware versions older than 4.
   // gpuGetGpuAsyncVersion() returns a signed ssize_t so ioctl failure (-1
   // with errno set, e.g., ENOTSUPP/ENOTTY when the driver lacks GPUAsync
   // support) is representable. Guard against < 0 explicitly before comparing
   // against the V4 threshold; otherwise a wrapped uint value would silently
   // pass the "< 4" check. Inspect errno for the specific ioctl error.
   {
      ssize_t fwVersion = gpuGetGpuAsyncVersion(ctx->fd);
      if (fwVersion < 0) {
         fprintf(stderr, "gpuGetGpuAsyncVersion ioctl failed: %s (errno=%d)\n",
                 strerror(errno), errno);
         goto fail_fd;
      }
      if (fwVersion < 4) {
         fprintf(stderr, "GpuAsyncCore version %zd < 4 is not supported\n",
                 fwVersion);
         goto fail_fd;
      }
   }

   // Step 7: Validate buffer count — must be positive and within firmware maximum.
   // Cache the ioctl result so the bound check and the error message do not
   // issue the GPU_Get_Max_Buffers ioctl twice. Like gpuGetGpuAsyncVersion(),
   // gpuGetMaxBuffers() returns a signed ssize_t so ioctl failure (-1 with
   // errno set) is representable without wrapping; check for < 0 before
   // comparing against bufCnt and inspect errno for the specific ioctl error.
   if (bufCnt <= 0) {
      fprintf(stderr, "bufCnt must be positive\n");
      goto fail_fd;
   }
   {
      ssize_t maxBuffers = gpuGetMaxBuffers(ctx->fd);
      if (maxBuffers < 0) {
         fprintf(stderr, "gpuGetMaxBuffers ioctl failed: %s (errno=%d)\n",
                 strerror(errno), errno);
         goto fail_fd;
      }
      if ((ssize_t)bufCnt > maxBuffers) {
         fprintf(stderr, "Too many buffers requested: %d > %zd\n",
                 bufCnt, maxBuffers);
         goto fail_fd;
      }
   }

   // Step 8: Create CUDA context
   {
#if CUDA_VERSION >= 13000
      CUresult cr = cuCtxCreate(&ctx->cuCtx, NULL, CU_CTX_SCHED_SPIN, computeDevice);
#else
      CUresult cr = cuCtxCreate(&ctx->cuCtx, CU_CTX_SCHED_SPIN, computeDevice);
#endif
      if (cr != CUDA_SUCCESS) {
         const char *en = NULL, *es = NULL;
         cuGetErrorName(cr, &en);
         cuGetErrorString(cr, &es);
         fprintf(stderr, "cuCtxCreate failed: %s (%s)\n",
                 en ? en : "?", es ? es : "?");
         goto fail_fd;
      }
   }

   // Step 9: Map FPGA registers into host address space
   ctx->regs = dmaMapRegister(ctx->fd, GPU_ASYNC_CORE_OFFSET, GPU_ASYNC_CORE_SIZE);
   if (ctx->regs == MAP_FAILED) {
      ctx->regs = NULL;
      fprintf(stderr, "Failed to map FPGA registers: %s (errno=%d)\n",
              strerror(errno), errno);
      goto fail_ctx;
   }

   // Step 10: Register mapped registers with CUDA for device access
   {
      CUresult cr = cuMemHostRegister(ctx->regs, GPU_ASYNC_CORE_SIZE,
                                      CU_MEMHOSTREGISTER_IOMEMORY | CU_MEMHOSTREGISTER_DEVICEMAP);
      if (cr != CUDA_SUCCESS) {
         const char *en = NULL, *es = NULL;
         cuGetErrorName(cr, &en);
         cuGetErrorString(cr, &es);
         fprintf(stderr, "cuMemHostRegister failed: %s (%s)\n",
                 en ? en : "?", es ? es : "?");
         goto fail_mapreg;
      }
   }

   // Step 11: Construct GpuAsyncCoreRegs in-place via placement new
   new (ctx->coreRegsStorage) GpuAsyncCoreRegs(ctx->regs);

   // Step 12: Disable write/read DMA engines before reconfiguring
   // (covers the case where a prior run left them enabled after a crash)
   gpuAsyncRegs(ctx)->setWriteEnable(0);
   gpuAsyncRegs(ctx)->setReadEnable(0);

   // Step 13: Store context fields
   ctx->bufCnt = bufCnt;
   ctx->bufSize = bufSize;
   ctx->loopback = loopback;
   ctx->dmaHeaderSize = gpuAsyncRegs(ctx)->dmaDataBytes();

   // Step 14: Compute device-side pointer to FPGA registers
   {
      CUresult cr = cuMemHostGetDevicePointer(&ctx->devRegs,
                                              (void*)gpuAsyncRegs(ctx)->registers(), 0);
      if (cr != CUDA_SUCCESS) {
         const char *en = NULL, *es = NULL;
         cuGetErrorName(cr, &en);
         cuGetErrorString(cr, &es);
         fprintf(stderr, "cuMemHostGetDevicePointer failed: %s (%s)\n",
                 en ? en : "?", es ? es : "?");
         goto fail_hostreg;
      }
   }

   // Step 15: Configure max FPGA->GPU transfer size
   gpuAsyncRegs(ctx)->setRemoteWriteMaxSize(0, bufSize);

   // Step 16: Allocate RX buffer array and cudaMalloc each buffer
   ctx->rxBuffers = (uint8_t**)malloc(bufCnt * sizeof(uint8_t*));
   if (!ctx->rxBuffers) {
      fprintf(stderr, "malloc failed for rxBuffers array (%d * %zu bytes): %s\n",
              bufCnt, sizeof(uint8_t*), strerror(errno));
      goto fail_hostreg;
   }
   memset(ctx->rxBuffers, 0, bufCnt * sizeof(uint8_t*));
   for (int i = 0; i < bufCnt; i++) {
      cudaError_t cr = cudaMalloc(&ctx->rxBuffers[i], bufSize + ctx->dmaHeaderSize);
      if (cr != cudaSuccess) {
         fprintf(stderr, "cudaMalloc failed for rxBuffers[%d]: %s (%s)\n",
                 i, cudaGetErrorName(cr), cudaGetErrorString(cr));
         goto fail_rxalloc;
      }
   }

   // Step 17: Allocate TX buffer array and cudaMalloc each buffer (loopback only)
   if (loopback) {
      ctx->txBuffers = (uint8_t**)malloc(bufCnt * sizeof(uint8_t*));
      if (!ctx->txBuffers) {
         fprintf(stderr, "malloc failed for txBuffers array (%d * %zu bytes): %s\n",
                 bufCnt, sizeof(uint8_t*), strerror(errno));
         goto fail_rxalloc;
      }
      memset(ctx->txBuffers, 0, bufCnt * sizeof(uint8_t*));
      for (int i = 0; i < bufCnt; i++) {
         cudaError_t cr = cudaMalloc(&ctx->txBuffers[i], bufSize + ctx->dmaHeaderSize);
         if (cr != cudaSuccess) {
            fprintf(stderr, "cudaMalloc failed for txBuffers[%d]: %s (%s)\n",
                    i, cudaGetErrorName(cr), cudaGetErrorString(cr));
            goto fail_txalloc;
         }
      }
   }

   // Step 18: Register GPU buffers with the FPGA
   for (int i = 0; i < bufCnt; i++) {
      if (gpuAddNvidiaMemory(ctx->fd, 1, (uint64_t)ctx->rxBuffers[i], bufSize) < 0) {
         fprintf(stderr, "gpuAddNvidiaMemory failed for rxBuffers[%d]: %s (errno=%d)\n",
                 i, strerror(errno), errno);
         goto fail_gpumem;
      }
   }
   if (loopback) {
      for (int i = 0; i < bufCnt; i++) {
         if (gpuAddNvidiaMemory(ctx->fd, 0, (uint64_t)ctx->txBuffers[i], bufSize) < 0) {
            fprintf(stderr, "gpuAddNvidiaMemory failed for txBuffers[%d]: %s (errno=%d)\n",
                    i, strerror(errno), errno);
            goto fail_gpumem;
         }
      }
   }

   // Step 19: Create CUDA stream
   {
      cudaError_t cr = cudaStreamCreate(&ctx->cuStream);
      if (cr != cudaSuccess) {
         fprintf(stderr, "cudaStreamCreate failed: %s (%s)\n",
                 cudaGetErrorName(cr), cudaGetErrorString(cr));
         goto fail_gpumem;
      }
   }

   // Step 20: Configure buffer counts on FPGA
   gpuAsyncRegs(ctx)->setWriteCount(bufCnt - 1);
   gpuAsyncRegs(ctx)->setReadCount(bufCnt - 1);

   // Step 21: Arm free list and clear doorbells via CUDA stream writes
   for (int i = 0; i < bufCnt; i++) {
      CUresult cr;
      const char *en = NULL, *es = NULL;
      if ((cr = cuStreamWriteValue32(ctx->cuStream,
                                     ctx->devRegs + gpuAsyncRegs(ctx)->freeListOffset(i),
                                     1, 0)) != CUDA_SUCCESS ||
          (cr = cuStreamWriteValue32(ctx->cuStream,
                                     (CUdeviceptr)ctx->rxBuffers[i] + 4, 0, 0)) != CUDA_SUCCESS ||
          (loopback && (cr = cuStreamWriteValue32(ctx->cuStream,
                                                  (CUdeviceptr)ctx->txBuffers[i], 1, 0)) != CUDA_SUCCESS)) {
         cuGetErrorName(cr, &en);
         cuGetErrorString(cr, &es);
         fprintf(stderr, "cuStreamWriteValue32 failed during free-list arming (buffer %d): %s (%s)\n",
                 i, en ? en : "?", es ? es : "?");
         goto fail_stream;
      }
   }

   // Step 21b: Flush the GPU stream before enabling FPGA engines. The arming
   // writes above target FPGA registers via the GPU's PCIe path, while Step 22
   // enables the engines via a host MMIO write. Without a barrier, the host
   // enable could reach the FPGA before the GPU's free-list writes land, and
   // the engines would start pulling from an unprimed free list. Blocking on
   // stream idle ensures the GPU has committed the writes before the enable.
   {
      CUresult cr = cuStreamSynchronize(ctx->cuStream);
      if (cr != CUDA_SUCCESS) {
         const char *en = NULL, *es = NULL;
         cuGetErrorName(cr, &en);
         cuGetErrorString(cr, &es);
         fprintf(stderr, "cuStreamSynchronize failed after free-list arming: %s (%s)\n",
                 en ? en : "?", es ? es : "?");
         goto fail_stream;
      }
   }

   // Step 22: Enable FPGA->GPU writes (and GPU->FPGA reads if loopback)
   gpuAsyncRegs(ctx)->setWriteEnable(1);
   if (loopback) gpuAsyncRegs(ctx)->setReadEnable(1);

   return 0;

fail_stream:
   cudaStreamDestroy(ctx->cuStream);
   ctx->cuStream = 0;
fail_gpumem:
   gpuRemNvidiaMemory(ctx->fd);
fail_txalloc:
   if (ctx->txBuffers) {
      for (int i = 0; i < bufCnt; i++) {
         if (ctx->txBuffers[i] != NULL) cudaFree(ctx->txBuffers[i]);
      }
      free(ctx->txBuffers);
      ctx->txBuffers = NULL;
   }
fail_rxalloc:
   if (ctx->rxBuffers) {
      for (int i = 0; i < bufCnt; i++) {
         if (ctx->rxBuffers[i] != NULL) cudaFree(ctx->rxBuffers[i]);
      }
      free(ctx->rxBuffers);
      ctx->rxBuffers = NULL;
   }
fail_hostreg:
   cuMemHostUnregister(ctx->regs);
fail_mapreg:
   dmaUnMapRegister(ctx->fd, ctx->regs, GPU_ASYNC_CORE_SIZE);
   ctx->regs = NULL;
   ctx->devRegs = 0;
fail_ctx:
   cuCtxDestroy(ctx->cuCtx);
   ctx->cuCtx = 0;
fail_fd:
   close(ctx->fd);
   ctx->fd = -1;
   return -1;
}

/**
 * @brief Tear down a GPU/FPGA session in strict reverse-init order.
 *
 * @details Disables write/read DMA engines, destroys the CUDA stream, removes
 * NVIDIA memory mappings, frees rx/tx GPU buffers and pointer arrays,
 * unregisters and unmaps the register block, closes the device file
 * descriptor, and destroys the CUDA context.
 *
 * Safe to call on a partially-initialised context (NULL/fd guards on every
 * resource). After return, every handle this function owns is reset
 * (fd = -1, cuCtx/cuStream/regs/rxBuffers/txBuffers = NULL, devRegs = 0);
 * scalar configuration fields (bufCnt, bufSize, dmaHeaderSize, loopback)
 * and coreRegsStorage are left untouched, so the caller must memset the
 * struct to zero before reusing it with another gpuAsyncInit() call.
 *
 * @param ctx Pointer to a GpuAsyncContext previously initialised by gpuAsyncInit.
 */
static inline void gpuAsyncCleanup(GpuAsyncContext* ctx) {
   // Step 22 reverse: disable FPGA DMA engines first
   if (ctx->regs != NULL) {
      gpuAsyncRegs(ctx)->setWriteEnable(0);
      gpuAsyncRegs(ctx)->setReadEnable(0);
   }

   // Step 19 reverse: destroy the CUDA stream
   if (ctx->cuStream) {
      cudaStreamDestroy(ctx->cuStream);
      ctx->cuStream = 0;
   }

   // Step 18 reverse: tear down all FPGA-side nvidia memory registrations
   if (ctx->fd >= 0)
      gpuRemNvidiaMemory(ctx->fd);

   // Step 17 reverse: free TX buffers (allocated after RX buffers in init)
   if (ctx->txBuffers != NULL) {
      for (int32_t i = 0; i < ctx->bufCnt; i++) {
         if (ctx->txBuffers[i] != NULL) cudaFree(ctx->txBuffers[i]);
      }
      free(ctx->txBuffers);
      ctx->txBuffers = NULL;
   }

   // Step 16 reverse: free RX buffers
   if (ctx->rxBuffers != NULL) {
      for (int32_t i = 0; i < ctx->bufCnt; i++) {
         if (ctx->rxBuffers[i] != NULL) cudaFree(ctx->rxBuffers[i]);
      }
      free(ctx->rxBuffers);
      ctx->rxBuffers = NULL;
   }

   // Steps 10 and 9 reverse: unregister from CUDA, unmap from host
   if (ctx->regs != NULL) {
      cuMemHostUnregister(ctx->regs);
      dmaUnMapRegister(ctx->fd, ctx->regs, GPU_ASYNC_CORE_SIZE);
      ctx->regs = NULL;
      ctx->devRegs = 0;
   }

   // Step 8 reverse: destroy the CUDA context (before closing fd, to match
   // the init unwind's fail_ctx -> fail_fd fall-through ordering)
   if (ctx->cuCtx) {
      cuCtxDestroy(ctx->cuCtx);
      ctx->cuCtx = 0;
   }

   // Step 4 reverse: close the DMA device fd last
   if (ctx->fd >= 0) {
      close(ctx->fd);
      ctx->fd = -1;
   }
}

#endif

#endif  // _GPU_ASYNC_USER_H_
