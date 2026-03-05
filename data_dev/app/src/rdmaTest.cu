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
#include "AppUtils.h"
#include "GpuAsyncRegs.h"
#include "GpuAsync.h"
#include "GpuAsyncUser.h"

static int s_verbose = 0;
static int s_dumpToFile = 0;
static int s_dumpBytes = 0;
static int s_cnt = -1;
static std::string s_dumpFile;

static void assertOk(cudaError_t err);
static void assertOk(CUresult err);
static void showHelp();

void runSimpleLoop(CUstream stream, GpuAsyncCoreRegs& regs, int bufCnt, uint8_t** buffs, uint8_t** readBuffs);

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
    bool fpgaRead = false;
    std::string dev = "/dev/datadev_0";
    while ((opt = getopt(argc, argv, "d:i:vhf:x:c:b:s:r")) != -1) {
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
        case 'r':
            fpgaRead = true;
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

    // Validate buffer count
    if (buffs > gpuGetMaxBuffers(fd)) {
        fprintf(stderr, "Too many buffers requested: %d > %d\n", buffs, gpuGetMaxBuffers(fd));
        return -1;
    }

    // Force read support off if we're on v3 or below
    if (fpgaRead && gpuGetGpuAsyncVersion(fd) < 4) {
        printf("GPU -> FPGA transfers are not supported on GpuAsyncCore V3 or below\n");
        fpgaRead = false;
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

    // Init a register object
    GpuAsyncCoreRegs coreRegs(regs);

    const uint32_t dmaHeaderSize = coreRegs.dmaDataBytes();

    // Allocate buffer space on the device
    uint8_t* buffers[buffs];
    memset(buffers, 0, buffs * sizeof(uint8_t*));
    for (int i = 0; i < buffs; ++i)
        assertOk(cudaMalloc(&buffers[i], size));

    // Allocate read buffers
    uint8_t* readBuffers[buffs];
    memset(readBuffers, 0, buffs * sizeof(uint8_t*));
    if (fpgaRead) {
        for (int i = 0; i < buffs; ++i) {
            // Size must always be 64k aligned, but we should be able to rely on CUDA to do that for us.
            assertOk(cudaMalloc(&readBuffers[i], size + dmaHeaderSize));
        }
    }

    // Add the buffers to the FPGA
    for (int i = 0; i < buffs; ++i) {
        if (gpuAddNvidiaMemory(fd, 1, (uint64_t)buffers[i], size) < 0) {
            fprintf(stderr, "gpuAddNvidiaMemory failed\n");
            return -1;
        }

        if (s_verbose)
            printf("Added write buffer at addr %p\n", buffers[i]);

        if (!readBuffers[i])
            continue;

        if (gpuAddNvidiaMemory(fd, 0, (uint64_t)readBuffers[i], size) < 0) {
            fprintf(stderr, "gpuAddNvidiaMemory failed\n");
            return -1;
        }

        if (s_verbose)
            printf("Added read buffer at addr 0x%p\n", readBuffers[i]);
    }

    // Create a stream to use
    CUstream stream;
    assertOk(cudaStreamCreate(&stream));

    runSimpleLoop(stream, coreRegs, buffs, buffers, readBuffers);

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
void runSimpleLoop(CUstream stream, GpuAsyncCoreRegs& regs, int bufCnt, uint8_t** buffs, uint8_t** readBuffs) {
    CUdeviceptr devRegs;
    assertOk(cuMemHostGetDevicePointer(&devRegs, (void*)regs.registers(), 0));

    uint64_t totalRecv = 0;
    uint64_t totalEvents = 0, invalidEvents = 0;

    uint8_t* tmpbuf = new uint8_t[s_dumpBytes ? s_dumpBytes : 1];

    float avgGpuLatency = 0, avgWrLatency = 0, avgTotLatency = 0;

    const uint32_t version = readGpuAsyncReg(regs.registers(), &GpuAsyncReg_Version);

    // Initialization pass: write 0x1 to all read doorbell registers
    if (readBuffs && *readBuffs) {
        for (int i = 0; i < bufCnt; ++i) {
            assertOk(cuStreamWriteValue32(stream, (CUdeviceptr)readBuffs[i], 1, 0));
        }
    }

    int curBuff = 0;
    while (s_cnt == -1 || s_cnt-- > 0) {
        AxiWrDesc64_t hdr;

        // Clear handshake area
        assertOk(cuStreamWriteValue32(stream, (CUdeviceptr)buffs[curBuff] + 4, 0, 0));

        // Indicate that we're ready for new data
        assertOk(cuStreamWriteValue32(stream, devRegs + regs.writeDetectOffset(curBuff), 1, 0));

        // Wait on handshake space for data
        assertOk(cuStreamWaitValue32(stream, (CUdeviceptr)buffs[curBuff] + 4, 1, CU_STREAM_WAIT_VALUE_GEQ));

        // Download header data immediately
        assertOk(cudaMemcpyAsync(&hdr, buffs[curBuff], sizeof(hdr), cudaMemcpyDeviceToHost, stream));

        // Read path (GPU -> FPGA)
        if (readBuffs && *readBuffs) {
            // Sync so header data becomes available to the host
            cuStreamSynchronize(stream);

            // Copy data we received from the FPGA to dedicated read buffers
            assertOk(cudaMemcpyAsync(readBuffs[curBuff] + regs.dmaDataBytes(), buffs[curBuff], hdr.size, cudaMemcpyDeviceToDevice));

            // Clear doorbell register
            assertOk(cuStreamWriteValue32(stream, (CUdeviceptr)readBuffs[curBuff], 0, 0));

            // Trigger immediate write from GPU -> FPGA. This uses the same buffer that was written to the GPU
            assertOk(cuStreamWriteValue32(stream, devRegs + regs.remoteReadSizeOffset(curBuff), hdr.size, 0));
            
            // Wait for the transfer to complete
            assertOk(cuStreamWaitValue32(stream, (CUdeviceptr)readBuffs[curBuff] + 4, 1, CU_STREAM_WAIT_VALUE_GEQ));
        }

        cuStreamSynchronize(stream);

        // Grab latency measurements
        const uint32_t gpuLatency = regs.gpuLatency(curBuff);
        const uint32_t totLatency = regs.totalLatency(curBuff);
        const uint32_t wrLatency = regs.writeLatency(curBuff);

        // Update averages
        avgGpuLatency = updateAverage<float>(avgGpuLatency, gpuLatency, totalEvents);
        avgTotLatency = updateAverage<float>(avgTotLatency, totLatency, totalEvents);
        avgWrLatency = updateAverage<float>(avgWrLatency, wrLatency, totalEvents);

        // Dump header data when requested
        if (s_verbose > 1) {
            printf(
                "hdr{size=%d, firstUser=%d, lastUser=%d, cont=%d, overflow=%d, result=%d} latency {wr=%u, gpu=%u, tot=%u}\n",
                hdr.size,
                hdr.firstUser(),
                hdr.lastUser(),
                hdr.cont(),
                hdr.overflow(),
                hdr.result(),
                wrLatency,
                gpuLatency,
                wrLatency
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
                "%-4lu events, %-4lu invalid events, %.2f MiB transferred. avgWrLatency=%d, avgGpuLatency=%d, avgTotLatency=%d \n",
                totalEvents,
                invalidEvents,
                double(totalRecv) / (1024. * 1024.),
                int32_t(avgWrLatency),
                int32_t(avgGpuLatency),
                int32_t(avgTotLatency)
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
    printf("  -r           : Enable GPU -> FPGA transactions (remote reads)\n");
    printf("  -v           : Increase verbosity. May be passed multiple times. -vv will enable dumping of DMA headers\n");
}