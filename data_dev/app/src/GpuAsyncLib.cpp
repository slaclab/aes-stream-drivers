/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Implementation of the GpuAsyncLib helpers declared in
 *  include/GpuAsyncLib.h. Originally pulled from
 *  slaclab/axi-pcie-devel software/gpu/common/GpuAsyncLib.cpp.
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

#include "GpuAsyncLib.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

static std::string errorString(CUresult res) {
    const char* ptr = nullptr;
    cuGetErrorName(res, &ptr);
    return std::string(ptr ? ptr : "unknown");
}

DataGPU::DataGPU(const char* path) {
    fd_ = open(path, O_RDWR);
    if (fd_ < 0) {
        fprintf(stderr, "DataGPU: failed to open %s: %s (errno=%d)\n",
                path, strerror(errno), errno);
        throw "DataGPU: open failed";
    }
}

CudaContext::CudaContext() : context_(nullptr), device_(0) {
    CUresult status;
    if ((status = cuInit(0)) != CUDA_SUCCESS) {
        fprintf(stderr, "CudaContext: cuInit failed: %d (%s)\n",
                status, errorString(status).c_str());
        throw "CudaContext: cuInit failed";
    }
}

bool CudaContext::init(int device, bool quiet) {
    int devs = 0;
    checkError(cuDeviceGetCount(&devs));
    if (!quiet) fprintf(stderr, "Total GPU devices %d\n", devs);
    if (devs <= 0) {
        fprintf(stderr, "No GPU devices available!\n");
        return false;
    }

    device = device < 0 ? 0 : device;
    if (devs <= device) {
        fprintf(stderr, "Invalid GPU device number %d! There are only %d devices available\n",
                device, devs);
        return false;
    }

    CUresult status;
    if ((status = cuDeviceGet(&device_, device)) != CUDA_SUCCESS) {
        fprintf(stderr, "Could not get GPU device! code=%d (%s)\n",
                status, errorString(status).c_str());
        return false;
    }

    char name[256];
    checkError(cuDeviceGetName(name, sizeof(name), device_));
    if (!quiet) fprintf(stderr, "Selected GPU device: %s\n", name);

    int res = 0;
    checkError(cuDeviceGetAttribute(&res, CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_MEM_OPS_V1,
                                    device_));
    if (!res) {
        fprintf(stderr,
                "WARNING: device does not support CUDA Stream Operations; this code may not run.\n"
                "  Set NVreg_EnableStreamMemOPs=1 when loading the NVIDIA kernel module if your GPU is supported.\n");
    }

    size_t globalMem = 0;
    checkError(cuDeviceTotalMem(&globalMem, device_));
    if (!quiet) fprintf(stderr, "Global memory: %llu MB\n",
                       (unsigned long long)(globalMem >> 20));

#if CUDA_VERSION >= 13000
    {
        CUctxCreateParams params = {};
        checkError(cuCtxCreate(&context_, &params, 0, device_));
    }
#else
    checkError(cuCtxCreate(&context_, 0, device_));
#endif

    return true;
}

void CudaContext::listDevices() {
    int devs = 0;
    if (cuDeviceGetCount(&devs) != CUDA_SUCCESS) {
        fprintf(stderr, "Unable to get GPU device count\n");
        return;
    }
    for (int i = 0; i < devs; ++i) {
        CUdevice dev;
        if (cuDeviceGet(&dev, i) != CUDA_SUCCESS) {
            fprintf(stderr, "Unable to get GPU device %d\n", i);
            continue;
        }
        char name[256];
        if (cuDeviceGetName(name, sizeof(name), dev) != CUDA_SUCCESS) {
            fprintf(stderr, "Unable to get name of GPU device %d\n", i);
            continue;
        }
        printf("%d: %s\n", i, name);
    }
}

int CudaContext::getAttribute(CUdevice_attribute attr) {
    int out = 0;
    if (cuDeviceGetAttribute(&out, attr, device_) == CUDA_SUCCESS)
        return out;
    return 0;
}

/* ----- Error helpers --------------------------------------------------- */

void checkError(CUresult status) {
    if (status != CUDA_SUCCESS) {
        const char* perrstr = nullptr;
        const char* perrnam = nullptr;
        cuGetErrorString(status, &perrstr);
        cuGetErrorName(status, &perrnam);
        fprintf(stderr, "CUDA driver error %s (%d): %s\n",
                perrnam ? perrnam : "?", status,
                perrstr ? perrstr : "unknown");
        abort();
    }
}

void checkError(cudaError_t status) {
    if (status != cudaSuccess) {
        fprintf(stderr, "CUDA runtime error %s (%d): %s\n",
                cudaGetErrorName(status), status, cudaGetErrorString(status));
        abort();
    }
}

bool wasError(CUresult status) {
    if (status != CUDA_SUCCESS) {
        const char* perrstr = nullptr;
        cuGetErrorString(status, &perrstr);
        fprintf(stderr, "CUDA driver error %d: %s\n",
                status, perrstr ? perrstr : "unknown");
        return true;
    }
    return false;
}

/* ----- Buffer state ---------------------------------------------------- */

int gpuInitBufferState(GpuBufferState_t* b, int fd, size_t bufSize) {
    if (gpuMapFpgaMem(&b->bwrite, fd, 0, bufSize, 1) != 0) {
        fprintf(stderr, "gpuMapFpgaMem(write): map failed\n");
        return -1;
    }
    if (gpuMapFpgaMem(&b->bread, fd, 0, bufSize, 0) != 0) {
        fprintf(stderr, "gpuMapFpgaMem(read): map failed\n");
        gpuUnmapFpgaMem(&b->bwrite);
        return -1;
    }
    return 0;
}

void gpuDestroyBufferState(GpuBufferState_t* b) {
    gpuUnmapFpgaMem(&b->bwrite);
    gpuUnmapFpgaMem(&b->bread);
}

/* ----- FPGA register mapping ------------------------------------------- */

int gpuMapHostFpgaMem(GpuDmaBuffer_t* outmem, int fd, uint64_t offset, size_t size) {
    memset(outmem, 0, sizeof(*outmem));

    outmem->ptr = (uint8_t*)dmaMapRegister(fd, offset, size);
    if (!outmem->ptr || outmem->ptr == (uint8_t*)MAP_FAILED) {
        outmem->ptr = nullptr;
        return -1;
    }
    outmem->size = size;
    outmem->fd = fd;

    CUresult status = cuMemHostRegister(outmem->ptr, size, CU_MEMHOSTREGISTER_IOMEMORY);
    if (wasError(status)) {
        fprintf(stderr, "cuMemHostRegister offset=0x%lx size=%zu failed: %s\n",
                (unsigned long)offset, size, errorString(status).c_str());
        dmaUnMapRegister(fd, outmem->ptr, outmem->size);
        memset(outmem, 0, sizeof(*outmem));
        return -1;
    }

    status = cuMemHostGetDevicePointer(&outmem->dptr, outmem->ptr, 0);
    if (wasError(status)) {
        fprintf(stderr, "cuMemHostGetDevicePointer failed: %s\n",
                errorString(status).c_str());
        cuMemHostUnregister(outmem->ptr);
        dmaUnMapRegister(fd, outmem->ptr, outmem->size);
        memset(outmem, 0, sizeof(*outmem));
        return -1;
    }

    int flag = 1;
    checkError(cuPointerSetAttribute(&flag, CU_POINTER_ATTRIBUTE_SYNC_MEMOPS, outmem->dptr));
    return 0;
}

int gpuMapFpgaMem(GpuDmaBuffer_t* outmem, int fd, uint64_t /*offset*/, size_t size, int write) {
    memset(outmem, 0, sizeof(*outmem));

    if ((size & 0xFFFF) != 0) {
        fprintf(stderr, "gpuMapFpgaMem: size 0x%zx is not a multiple of 64 KiB\n", size);
        return -1;
    }

    uint8_t* dp = nullptr;
    cudaError_t err = cudaMalloc(&dp, size);
    if (err != cudaSuccess) {
        fprintf(stderr, "cudaMalloc(%zu) failed: %s\n",
                size, cudaGetErrorString(err));
        return -1;
    }
    outmem->dptr = reinterpret_cast<CUdeviceptr>(dp);
    outmem->size = size;
    cudaMemset((void*)outmem->dptr, 0, size);

    int flag = 1;
    CUresult result = cuPointerSetAttribute(&flag, CU_POINTER_ATTRIBUTE_SYNC_MEMOPS,
                                            outmem->dptr);
    if (result != CUDA_SUCCESS) {
        fprintf(stderr, "cuPointerSetAttribute failed: %s\n", errorString(result).c_str());
        cuMemFree(outmem->dptr);
        memset(outmem, 0, sizeof(*outmem));
        return -1;
    }

    ssize_t r = gpuAddNvidiaMemory(fd, write, outmem->dptr, outmem->size);
    if (r < 0) {
        fprintf(stderr, "gpuAddNvidiaMemory failed: %s (errno=%d)\n",
                strerror(errno), errno);
        cuMemFree(outmem->dptr);
        memset(outmem, 0, sizeof(*outmem));
        return -1;
    }

    outmem->gpuOnly = 1;
    outmem->fd = fd;
    return 0;
}

void gpuUnmapFpgaMem(GpuDmaBuffer_t* mem) {
    if (!mem) return;
    if (!mem->gpuOnly && mem->ptr) {
        cuMemHostUnregister(mem->ptr);
        dmaUnMapRegister(mem->fd, mem->ptr, mem->size);
        mem->ptr = nullptr;
    }
    if (mem->dptr) {
        cuMemFree(mem->dptr);
        mem->dptr = 0;
    }
    mem->size = 0;
    mem->fd = 0;
}
