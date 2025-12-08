/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    Tests basic functionality of GPUDirect RDMA with compatible firmwares
 *-----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to the
 * license terms in the LICENSE.txt file found in the top-level directory of
 * this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the aes_stream_drivers package, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 *-----------------------------------------------------------------------------
**/

#include <cuda.h>
#include <getopt.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <thread>
#include <fstream>
#include <algorithm>

#include "DmaDriver.h"
#include "GpuAsyncRegs.h"
#include "GpuAsync.h"

static int s_verbose = 0;
static int s_dumpToFile = 0;
static int s_dumpBytes = 0;
static int s_cnt = -1;
static std::string s_dumpFile;

static void assertOk(cudaError_t err);
static void assertOk(CUresult err);
static void showHelp();

void runSimpleLoop(CUstream stream, uint8_t* regs, int bufCnt, uint8_t** buffs);

/**
 * 64-bit AXI write descriptor
 * ref: https://github.com/slaclab/surf/blob/main/axi/dma/rtl/v2/AxiStreamDmaV2Write.vhd#L443-L449
 */
struct __attribute__((packed)) AxiWrDesc64_t
{
    uint32_t header;
    uint32_t size;

    inline uint32_t result() const { return header & 0x3; }
    inline uint32_t overflow() const { return (header >> 2) & 0x1; }
    inline uint32_t cont() const { return (header >> 3) & 0x1; }
    inline uint32_t lastUser() const { return (header >> 16) & 0xFF; }
    inline uint32_t firstUser() const { return (header >> 24) & 0xFF; }
};

static_assert(sizeof(AxiWrDesc64_t) == 8, "AxiWrDesc64_t must be 64-bits (8-bytes)");

int str2int(const char* s) {
    int base = 10;
    if (*s == '0' && s[1] == 'x')
        base = 16;
    return strtol(s, NULL, base);
}

int main(int argc, char** argv) {
    int index = 0, ret = 0;
    if ((ret = cuInit(0)) != CUDA_SUCCESS) {
        fprintf(stderr, "cuInit failed (%i)\n", ret);
        return -1;
    }

    int devCount;
    if ((ret = cudaGetDeviceCount(&devCount)) != cudaSuccess) {
        fprintf(stderr, "cudaGetDeviceCount: %s (%i)\n", cudaGetErrorString((cudaError_t)ret), ret);
        return -1;
    }

    int opt, buffs = 1, size = 0x10000;
    std::string dev = "/dev/datadev_0";
    while ((opt = getopt(argc, argv, "d:i:vhf:x:c:b:")) != -1) {
        switch (opt) {
        case 'd':
            dev = optarg;
            break;
        case 'b':
            buffs = str2int(optarg);
            break;
        case 's':
            size = str2int(optarg);
            break;
        case 'f':
            s_dumpFile = optarg;
            break;
        case 'x':
            s_dumpBytes = str2int(optarg);
            break;
        case 'v':
            s_verbose++;
            break;
        case 'c':
            s_cnt = str2int(optarg);
            break;
        case 'h':
            showHelp();
            return 0;
        case 'i':
            index = atoi(optarg);
            if (index < 0 || index >= devCount) {
                fprintf(stderr, "CUDA device index %d is out of range (%d devices available on this system)\n", index, devCount);
                return 1;
            }
            break;
        default:
            showHelp();
            return 1;
            break;
        }
    }
    
    if (buffs > 4) {
        fprintf(stderr, "Max buffers exceeded\n");
        return -1;
    }

    CUcontext ctx;
    CUdevice computeDevice;

    // Attempt to get the user specified device
    if (cuDeviceGet(&computeDevice, index) != CUDA_SUCCESS) {
        fprintf(stderr, "Failed to get cuda device with index %i\n", index);
        return -1;
    }
    
    // Ensure stream mem ops are available
    int pi;
    cuDeviceGetAttribute(&pi, CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_MEM_OPS_V1, computeDevice);
    if (pi != 1) {
        fprintf(stderr, "Device %d cannot use Stream mem ops!\n", index);
        return -1;
    }

    // Open the DMA device
    int fd = open(dev.c_str(), O_RDWR);
    if (fd < 0) {
        perror("open");
        return -1;
    }
    
    // Check for GPUDirect support
    if (!gpuIsGpuAsyncSupported(fd)) {
        printf("Firmware or driver does not support GPUAsync!\n");
        close(fd);
        return -1;
    }

    // Create a new context
    if ((ret = cuCtxCreate(&ctx, NULL, CU_CTX_SCHED_SPIN, computeDevice)) != CUDA_SUCCESS) {
        fprintf(stderr, "Failed to create cuda context (%i)\n", ret);
        return -1;
    }

    // Map device control registers to the host
    void* regs = dmaMapRegister(fd, GPU_ASYNC_CORE_OFFSET, 0x10000);
    if (!regs) {
        fprintf(stderr, "Failed to map FPGA registers\n");
        return -1;
    }

    // Map the registers into the CUDA address space to allow the GPU to access them
    if ((ret = cuMemHostRegister(regs, 0x10000, CU_MEMHOSTREGISTER_IOMEMORY | CU_MEMHOSTREGISTER_DEVICEMAP)) != CUDA_SUCCESS) {
        fprintf(stderr, "cuMemHostRegister failed: %d. You may have to run the application as root\n", ret);
        return -1;
    }
    
    // Allocate buffer space on the device
    uint8_t* buffers[buffs] = {0};
    for (int i = 0; i < buffs; ++i)
        assertOk(cudaMalloc(&buffers[i], size));
    
    // Add the buffers to the FPGA
    for (int i = 0; i < buffs; ++i) {
        if (gpuAddNvidiaMemory(fd, 1, (uint64_t)buffers[i], size) < 0) {
            fprintf(stderr, "gpuAddNvidiaMemory failed\n");
            return -1;
        }
    }

    // Create a stream to use
    CUstream stream;
    assertOk(cudaStreamCreate(&stream));

    runSimpleLoop(stream, (uint8_t*)regs, buffs, buffers);

    // Kill off stream
    cudaStreamDestroy(stream);

    // Remove all GPU buffers from FPGA
    gpuRemNvidiaMemory(fd);

    // Free all buffers
    for (int i = 0; i < buffs; ++i)
        cudaFree(buffers[i]);

    // Unmap FPGA mem
    cuMemHostUnregister(regs);
    dmaUnMapRegister(fd, regs, 0x10000);
    close(fd);

    cuCtxDestroy(ctx);
    
    return 0;
}

/**
 * Run a simple test receiving data from the FPGA, optionally decoding the header or
 * dumping event data to file.
 */
void runSimpleLoop(CUstream stream, uint8_t* regs, int bufCnt, uint8_t** buffs) {
    CUdeviceptr devRegs;
    assertOk(cuMemHostGetDevicePointer(&devRegs, regs, 0));

    uint64_t totalRecv = 0;
    uint64_t totalEvents = 0, invalidEvents = 0;

    uint8_t* tmpbuf = new uint8_t[s_dumpBytes ? s_dumpBytes : 1];

    int curBuff = 0;
    while (s_cnt == -1 || s_cnt-- > 0) {
        // Clean handshake area
        assertOk(cuStreamWriteValue32(stream, (CUdeviceptr)buffs[curBuff] + 4, 0, 0));

        // Indicate that we're ready for new data
        assertOk(cuStreamWriteValue32(stream, devRegs + GPU_ASYNC_REG_WRITE_DETECT_OFFSET(curBuff), 1, 0));

        // Wait on handshake space for data
        assertOk(cuStreamWaitValue32(stream, (CUdeviceptr)buffs[curBuff] + 4, 1, CU_STREAM_WAIT_VALUE_GEQ));

        cuStreamSynchronize(stream);

        // Unpack AXI transaction header
        AxiWrDesc64_t hdr;
        assertOk(cudaMemcpy(&hdr, buffs[curBuff], sizeof(hdr), cudaMemcpyDeviceToHost));

        // Dump header data when requested
        if (s_verbose > 1) {
            printf(
                "hdr{size=%d, firstUser=%d, lastUser=%d, cont=%d, overflow=%d, result=%d}\n",
                hdr.size,
                hdr.firstUser(),
                hdr.lastUser(),
                hdr.cont(),
                hdr.overflow(),
                hdr.result()
            );
        }

        // Dump first N bytes when requested
        if (s_dumpBytes) {
            size_t count = std::min((uint32_t)s_dumpBytes, hdr.size);
            assertOk(cudaMemcpy(tmpbuf, buffs[curBuff], count, cudaMemcpyDeviceToHost));
            
            for (int i = 0; i < count; ++i) {
                printf("%02X ", tmpbuf[i]);
                if (i && (i+1) % 32 == 0)
                    printf("\n");
            }
            printf("\n");
        }

        // Dump first event to file
        if (!s_dumpFile.empty() && !s_dumpToFile) {
            std::ofstream file;
            file.open(s_dumpFile.c_str(), std::ios::binary | std::ios::out);
            if (file.good()) {
                file.write((char*)tmpbuf, hdr.size);
                fprintf(stderr, "Dumped event data to %s\n", s_dumpFile.c_str());
            }
            else
                fprintf(stderr, "Failed to dump event data to %s\n", s_dumpFile.c_str());
            s_dumpToFile = 1;
        }

        totalEvents++;
        totalRecv += hdr.size;

        // Round-robin to the next buffer
        curBuff++;
        if (curBuff >= bufCnt)
            curBuff = 0;

        // Status updates every 1024 events
        if (totalEvents % 1024 == 0) {
            printf(
                "%-4lu events, %-4lu invalid events, %.2f MiB transferred\n",
                totalEvents,
                invalidEvents,
                double(totalRecv) / (1024. * 1024.)
            );
        }
    }
}

static void assertOk(cudaError_t err) {
    if (err != cudaSuccess) {
        printf("Function failed: %s (%i)\n", cudaGetErrorString(err), err);
        abort();
    }
}

static void assertOk(CUresult err) {
    if (err != CUDA_SUCCESS) {
        printf("Function failed: %i\n", err);
        abort();
    }
}

static void showHelp() {
    printf("USAGE: rdmaTest [-d DEVICE] [-i GPU] [-b BUFFERS] [-s SIZE] [-v]\n");
    printf("  -d DEVICE    : Path to the datadev device (default /dev/datadev_0)\n");
    printf("  -i GPU       : GPU index\n");
    printf("  -s SIZE      : Transfer size\n");
    printf("  -b BUFFS     : Number of buffers to round-robin with\n");
    printf("  -f FILE      : Dump the first event received to this file\n");
    printf("  -x NUM       : Dump the first NUM bytes of the payload to stdout\n");
    printf("  -c CNT       : Number of events to receive before exiting\n");
    printf("  -v           : Increase verbosity. May be passed multiple times. -vv will enable dumping of DMA headers\n");
}