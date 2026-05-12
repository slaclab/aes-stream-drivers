/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Thin C++ helpers for CUDA + GpuAsync. Wraps the basic
 *  boilerplate (DataGPU device handle, CudaContext init, FPGA<->GPU
 *  memory mapping via cuMemHostRegister or cudaMalloc + gpuAddNvidiaMemory)
 *  without prescribing a session lifecycle. Originally pulled from
 *  slaclab/axi-pcie-devel software/gpu/common/GpuAsyncLib.h.
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
#ifndef _AES_STREAM_DRIVERS_GPU_ASYNC_LIB_H_
#define _AES_STREAM_DRIVERS_GPU_ASYNC_LIB_H_

#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>
#include <cuda.h>
#include <cuda_runtime.h>

#include <string>

#include "DmaDriver.h"
#include "GpuAsync.h"

#ifdef __CUDACC__
#define globalFunc __global__
#define hostFunc   __host__
#define deviceFunc __device__
#else
#define deviceFunc
#define globalFunc
#define hostFunc
#endif

#if defined(__CUDACC_VER_MAJOR__) && (__CUDACC_VER_MAJOR__ < 12)
/* Older CUDA toolchains predate the V1 attribute name. */
#define CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_MEM_OPS_V1 \
        CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_MEM_OPS
#endif

/* ----- Error helpers ---------------------------------------------------- */

/** @brief Print + abort if @p status is not CUDA_SUCCESS. */
void checkError(CUresult status);
/** @brief Print + abort if @p status is not cudaSuccess. */
void checkError(cudaError_t status);
/** @brief Return true (and print) if @p status is not CUDA_SUCCESS. */
bool wasError(CUresult status);

/* ----- DataGPU device handle ------------------------------------------- */

/**
 * @brief RAII wrapper around an opened /dev/datadev_X file descriptor.
 *
 * The constructor opens the device with O_RDWR and throws on failure.
 * The destructor closes the descriptor.
 */
class DataGPU {
public:
    explicit DataGPU(const char* path);
    ~DataGPU() { if (fd_ >= 0) close(fd_); }

    /** @brief Returns the underlying file descriptor (or -1 if closed). */
    int fd() const { return fd_; }

protected:
    int fd_;
};

/* ----- CUDA context ----------------------------------------------------- */

/**
 * @brief Wraps cuInit + cuDeviceGet + cuCtxCreate.
 *
 * The constructor calls cuInit(0) and throws on failure. init() selects a
 * device, verifies stream-memory-ops, and creates a CUDA context. The
 * resulting context and device are exposed via context() and device().
 */
class CudaContext {
public:
    CudaContext();

    /**
     * @brief Select a CUDA device and create a context.
     * @param device CUDA device index. < 0 selects device 0.
     * @param quiet  Suppress informational stderr output.
     * @return true on success.
     */
    bool init(int device = -1, bool quiet = false);

    /** @brief Print all visible CUDA devices to stdout. */
    void listDevices();

    /** @brief Convenience wrapper around cuDeviceGetAttribute (returns 0 on failure). */
    int getAttribute(CUdevice_attribute attr);

    CUdevice  device()  const { return device_; }
    CUcontext context() const { return context_; }

protected:
    CUcontext context_;
    CUdevice  device_;
};

/* ----- DMA buffers ----------------------------------------------------- */

/**
 * @brief FPGA memory mapped for GPU access (host-mapped or RDMA).
 */
struct GpuDmaBuffer_t {
    int         fd;        /**< Owning DMA device fd. */
    uint8_t*    ptr;       /**< Host-accessible pointer (NULL when gpuOnly == 1). */
    size_t      size;      /**< Size of the block in bytes. */
    CUdeviceptr dptr;      /**< Device pointer for the block. */
    int         gpuOnly;   /**< 1 if FPGA<->GPU only (no host mapping). */
};

/**
 * @brief Map an FPGA register block to host + GPU via cuMemHostRegister.
 *
 * Use for control / register access — not high-throughput data. Backed by
 * dmaMapRegister(). The caller must call gpuUnmapFpgaMem() to release.
 *
 * @param outmem Output buffer descriptor (zero-initialised on entry).
 * @param fd     DMA device fd.
 * @param offset Register-block offset within the device.
 * @param size   Register-block size in bytes.
 * @return 0 on success, -1 on failure.
 */
int gpuMapHostFpgaMem(GpuDmaBuffer_t* outmem, int fd, uint64_t offset, size_t size);

/**
 * @brief Allocate a GPU buffer and register it with the FPGA via RDMA.
 *
 * Backed by cudaMalloc + cuPointerSetAttribute + gpuAddNvidiaMemory.
 *
 * @param outmem Output buffer descriptor (zero-initialised on entry).
 * @param fd     DMA device fd.
 * @param offset Currently unused; reserved for future use.
 * @param size   Buffer size in bytes (must be a multiple of 64 KiB).
 * @param write  Non-zero if this buffer is for FPGA->GPU writes; zero for GPU->FPGA reads.
 * @return 0 on success, -1 on failure.
 */
int gpuMapFpgaMem(GpuDmaBuffer_t* outmem, int fd, uint64_t offset, size_t size, int write);

/**
 * @brief Release a buffer previously returned by gpuMapHostFpgaMem / gpuMapFpgaMem.
 */
void gpuUnmapFpgaMem(GpuDmaBuffer_t* mem);

/* ----- Paired RX/TX buffer state --------------------------------------- */

/**
 * @brief A pair of FPGA-mapped GPU buffers: one for FPGA writes, one for FPGA reads.
 */
struct GpuBufferState_t {
    uint8_t*       swFpgaRegs;
    GpuDmaBuffer_t bread;   /**< FPGA -> GPU read buffer (GPU is the destination of FPGA reads). */
    GpuDmaBuffer_t bwrite;  /**< FPGA -> GPU write buffer (GPU is the destination of FPGA writes). */
};

/**
 * @brief Allocate paired rx + tx GPU buffers and register them with the FPGA.
 * @return 0 on success, -1 on error (rx already cleaned up if tx failed).
 */
int  gpuInitBufferState(GpuBufferState_t* b, int fd, size_t bufSize);
/** @brief Tear down state allocated by gpuInitBufferState. */
void gpuDestroyBufferState(GpuBufferState_t* b);

/* ----- AXI-stream descriptor ------------------------------------------- */

/**
 * @brief 64-bit AXI stream write descriptor as emitted by the FPGA.
 * Layout matches AxiStreamDmaV2Write.vhd.
 */
struct __attribute__((packed)) AxiWrDesc64_t {
   uint32_t flags;
   uint32_t size;

   // Accessors for frame flags. Cannot use bit flags due to potential reordering by the compiler.
   deviceFunc hostFunc inline uint32_t result() const { return flags & 0x3; }
   deviceFunc hostFunc inline uint32_t overflow() const { return !!(flags & 0x4); }
   deviceFunc hostFunc inline uint32_t cont() const { return !!(flags & 0x8); }
   deviceFunc hostFunc inline uint32_t lastUser() const { return (flags >> 16) & 0xFF; }
   deviceFunc hostFunc inline uint32_t firstUser() const { return (flags >> 24) & 0xFF; }
};

static_assert(sizeof(AxiWrDesc64_t) == 8, "AxiWrDesc64_t must be 64-bits (8-bytes)");

deviceFunc inline AxiWrDesc64_t UnpackAxiWriteDescriptor(const void* data) {
    return *(const AxiWrDesc64_t*)data;
}

#endif  // _AES_STREAM_DRIVERS_GPU_ASYNC_LIB_H_
