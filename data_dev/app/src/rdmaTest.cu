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
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

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

void runSimpleLoop(GpuAsyncContext* ctx);

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
    int index = 0;
    {
        CUresult cr = cuInit(0);
        if (cr != CUDA_SUCCESS) {
            const char *en = NULL, *es = NULL;
            cuGetErrorName(cr, &en);
            cuGetErrorString(cr, &es);
            fprintf(stderr, "cuInit failed: %s (%s)\n",
                    en ? en : "?", es ? es : "?");
            return 1;
        }
    }

    int devCount;
    {
        cudaError_t cr = cudaGetDeviceCount(&devCount);
        if (cr != cudaSuccess) {
            fprintf(stderr, "cudaGetDeviceCount failed: %s (%s)\n",
                    cudaGetErrorName(cr), cudaGetErrorString(cr));
            return 1;
        }
    }

    int opt, bufCnt = 1024, size = 0x100000;
    bool loopback = false;
    std::string dev = "/dev/datadev_0";
    while ((opt = getopt(argc, argv, "d:i:vhf:x:c:b:s:l")) != -1) {
        switch (opt) {
        case 'd':
            dev = optarg;
            break;
        case 'b':
            bufCnt = str2int(optarg);
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
        case 'l':
            loopback = true;
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

    GpuAsyncContext gpuCtx;
    memset(&gpuCtx, 0, sizeof(gpuCtx));
    gpuCtx.fd = -1;
    if (gpuAsyncInit(&gpuCtx, dev.c_str(), index, bufCnt, size, loopback ? 1 : 0) < 0) {
        return 1;
    }

    // Run the primary function routine
    runSimpleLoop(&gpuCtx);

    gpuAsyncCleanup(&gpuCtx);
    return 0;
}

/**
 * Run a simple test receiving data from the FPGA, optionally decoding the header or
 * dumping event data to file.
 */
void runSimpleLoop(GpuAsyncContext* ctx) {
    // Create and init variables
    uint64_t totalRecv = 0;
    uint64_t totalEvents = 0, invalidEvents = 0;
    int curBuff = 0;
    const size_t dumpBytes = (s_dumpBytes > 0) ? static_cast<size_t>(s_dumpBytes) : 0U;
    std::vector<uint8_t> tmpbuf(dumpBytes);

    // Enter the processing loop
    while (s_cnt == -1 || s_cnt-- > 0) {
        AxiWrDesc64_t hdr;

        // Wait on handshake space on the GPU side (A.K.A. "GPU's doorbell")
        assertOk(cuStreamWaitValue32(ctx->cuStream, (CUdeviceptr)ctx->rxBuffers[curBuff] + 4, 1, CU_STREAM_WAIT_VALUE_GEQ));

        // Download header data immediately
        assertOk(cudaMemcpyAsync(&hdr, ctx->rxBuffers[curBuff], sizeof(hdr), cudaMemcpyDeviceToHost, ctx->cuStream));

        // SYNC the stream so header data becomes available to the host.
        // Check the return value: a failed sync would leave `hdr` undefined
        // and race the subsequent doorbell writes.
        assertOk(cuStreamSynchronize(ctx->cuStream));

        // TX path (GPU -> FPGA)
        if (ctx->loopback) {
            // Validate hdr.size before issuing the rx->tx device copy.
            // The copy destination is txBuffers[curBuff] + dmaHeaderSize inside a
            // bufSize+dmaHeaderSize allocation, so the safe upper bound is bufSize.
            // A corrupt descriptor with hdr.size > bufSize would overrun the tx
            // buffer; hdr.size == 0 would trigger a zero-byte copy and a nonsense
            // remoteReadSize doorbell. Drop the loopback for this event in either
            // case; the outer rx doorbell clear / rx free-list refill still run so
            // the FPGA rx pipeline stays alive.
            const uint32_t maxPayload = static_cast<uint32_t>(ctx->bufSize);
            if (hdr.size == 0 || hdr.size > maxPayload) {
                fprintf(stderr,
                        "Dropping loopback: invalid hdr.size=%u (expected 1..%u)\n",
                        hdr.size, maxPayload);
                invalidEvents++;
            } else {
                // Wait for the FPGA to return the free list back to GPU
                assertOk(cuStreamWaitValue32(ctx->cuStream, (CUdeviceptr)ctx->txBuffers[curBuff], 1, CU_STREAM_WAIT_VALUE_GEQ));

                // Copy rxData to the txData buffer for this loopback mode at
                // the dmaDataBytes() payload offset. Use the cached
                // ctx->dmaHeaderSize (populated once at init) instead of
                // re-reading the FPGA register every event.
                assertOk(cudaMemcpyAsync(ctx->txBuffers[curBuff] + ctx->dmaHeaderSize, ctx->rxBuffers[curBuff] + ctx->dmaHeaderSize, hdr.size, cudaMemcpyDeviceToDevice, ctx->cuStream));

                // Remove from free list on the GPU side  (A.K.A. "GPU's free list")
                assertOk(cuStreamWriteValue32(ctx->cuStream, (CUdeviceptr)ctx->txBuffers[curBuff], 0, 0));

                // SYNC the stream for the data copy and GPU side free list update before trigginer the FPGA's doorbell
                assertOk(cuStreamSynchronize(ctx->cuStream));

                // Trigger the FPGA to read the txBuffers from the GPU (A.K.A. "FPGA's doorbell")
                assertOk(cuStreamWriteValue32(ctx->cuStream, ctx->devRegs + gpuAsyncRegs(ctx)->remoteReadSizeOffset(curBuff), hdr.size, 0));
            }
        }

        // Clear handshake space on the GPU side (A.K.A. "GPU's doorbell")
        assertOk(cuStreamWriteValue32(ctx->cuStream, (CUdeviceptr)ctx->rxBuffers[curBuff] + 4, 0, 0));

        // After the rxData->txData copy, return the buffer index back to the FPGA side (A.K.A. "FPGA's free list")
        assertOk(cuStreamWriteValue32(ctx->cuStream, ctx->devRegs + gpuAsyncRegs(ctx)->freeListOffset(curBuff), 1, 0));

        // Dump header data when requested. hdr.size and the bit-field
        // accessors all return uint32_t, so use %u not %d.
        if (s_verbose > 1) {
            printf(
                "hdr{size=%u, firstUser=%u, lastUser=%u, cont=%u, overflow=%u, result=%u}\n",
                hdr.size,
                hdr.firstUser(),
                hdr.lastUser(),
                hdr.cont(),
                hdr.overflow(),
                hdr.result()
            );
        }

        // Dump first N bytes when requested. Clamp the copy size to the
        // actual per-buffer cudaMalloc allocation (bufSize + dmaHeaderSize) so
        // that a corrupt hdr.size combined with a large user-supplied -x cannot
        // trigger an out-of-bounds device-to-host copy.
        if (dumpBytes != 0U) {
            const size_t maxCount = static_cast<size_t>(ctx->bufSize) +
                                    static_cast<size_t>(ctx->dmaHeaderSize);
            size_t count = std::min(std::min(static_cast<size_t>(hdr.size),
                                             dumpBytes),
                                    maxCount);
            assertOk(cudaMemcpy(tmpbuf.data(), ctx->rxBuffers[curBuff], count, cudaMemcpyDeviceToHost));

            for (size_t i = 0; i < count; ++i) {
                printf("%02X ", tmpbuf[i]);
                if (i && (i+1) % 32 == 0)
                    printf("\n");
            }
            printf("\n");
        }

        // Dump first event to file
        if (!s_dumpFile.empty() && !s_dumpToFile) {
            // Compute the per-buffer allocation bound in size_t to match the
            // -x dump path above and avoid a latent uint32_t wraparound when
            // bufSize + dmaHeaderSize approaches 2^32.
            const size_t maxBytes = static_cast<size_t>(ctx->bufSize) +
                                    static_cast<size_t>(ctx->dmaHeaderSize);
            if (hdr.size == 0 || hdr.size > maxBytes) {
                fprintf(stderr,
                        "Skipping dump: invalid hdr.size=%u (expected 1..%zu)\n",
                        hdr.size, maxBytes);
            } else {
                std::vector<uint8_t> filebuf(hdr.size);
                assertOk(cudaMemcpy(filebuf.data(), ctx->rxBuffers[curBuff], hdr.size, cudaMemcpyDeviceToHost));
                std::ofstream file;
                file.open(s_dumpFile.c_str(), std::ios::binary | std::ios::out);
                if (file.good()) {
                    file.write((char*)filebuf.data(), hdr.size);
                    fprintf(stderr, "Dumped event data to %s\n", s_dumpFile.c_str());
                }
                else
                    fprintf(stderr, "Failed to dump event data to %s\n", s_dumpFile.c_str());
                s_dumpToFile = 1;
            }
        }

        totalEvents++;
        totalRecv += (uint64_t)hdr.size;

        // Round-robin to the next buffer
        curBuff++;
        if (curBuff >= ctx->bufCnt) {
            curBuff = 0;
        }

        // Status updates every 65536 events
        if (totalEvents % 65536 == 0) {
            printf(
                "%-4lu events, %-4lu invalid events, %.2f GiB transferred\n",
                totalEvents,
                invalidEvents,
                double(totalRecv)/1.0E+9
            );
        }

    }

}

static void assertOk(cudaError_t err) {
    if (err != cudaSuccess) {
        fprintf(stderr, "CUDA runtime API call failed: %s (%s)\n",
                cudaGetErrorName(err), cudaGetErrorString(err));
        abort();
    }
}

static void assertOk(CUresult err) {
    if (err != CUDA_SUCCESS) {
        const char *en = NULL, *es = NULL;
        cuGetErrorName(err, &en);
        cuGetErrorString(err, &es);
        fprintf(stderr, "CUDA driver API call failed: %s (%s)\n",
                en ? en : "?", es ? es : "?");
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
    printf("  -l           : Enable loopback mode: FPGA -> GPU -> FPGA transactions\n");
    printf("  -v           : Increase verbosity. May be passed multiple times. -vv will enable dumping of DMA headers\n");
}