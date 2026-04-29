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
#include <cuda_runtime.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include "DmaDriver.h"
#include "GpuAsync.h"
#include "GpuAsyncRegs.h"
#include "GpuAsyncUser.h"
#include "GpuAsyncLib.h"

static int s_verbose = 0;
static int s_dumpToFile = 0;
static int s_dumpBytes = 0;
static int s_cnt = -1;
static std::string s_dumpFile;

static void assertOk(cudaError_t err);
static void assertOk(CUresult err);
static void showHelp();

static int str2int(const char* s) {
    int base = 10;
    if (*s == '0' && s[1] == 'x')
        base = 16;
    return strtol(s, NULL, base);
}

/**
 * @brief Per-session state. Replaces the old GpuAsyncContext lifecycle wrapper
 * that this test used to consume from GpuAsyncUser.h. Held entirely on the
 * stack of main() and torn down explicitly at end-of-test.
 */
struct TestSession {
    DataGPU* dataGpu = nullptr;          ///< Owns /dev/datadev_X fd via RAII.
    CudaContext cuda;                    ///< Owns CUDA device + context.
    GpuDmaBuffer_t regs = {};            ///< FPGA register block (host-mapped + GPU-mapped).
    GpuAsyncCoreRegs* coreRegs = nullptr;///< View over regs, knows V1/V4 layout.
    CUstream stream = 0;                 ///< Single stream used for the simple-loop test.
    std::vector<uint8_t*> rxBuffers;     ///< FPGA->GPU buffers (cudaMalloc'd, registered with FPGA).
    std::vector<uint8_t*> txBuffers;     ///< GPU->FPGA buffers, only populated when loopback is set.
    int bufCnt = 0;
    int bufSize = 0;
    uint32_t dmaHeaderSize = 0;
    bool loopback = false;
};

static void runSimpleLoop(TestSession& s);
static int  initSession(TestSession& s, const char* dev, int gpuIdx,
                        int bufCnt, int bufSize, bool loopback);
static void cleanupSession(TestSession& s);

int main(int argc, char** argv) {
    int gpuIdx = 0;
    int bufCnt = 1024;
    int bufSize = 0x100000;
    bool loopback = false;
    std::string dev = "/dev/datadev_0";

    int opt;
    while ((opt = getopt(argc, argv, "d:i:vhf:x:c:b:s:l")) != -1) {
        switch (opt) {
        case 'd': dev = optarg;                 break;
        case 'b': bufCnt = str2int(optarg);     break;
        case 's': bufSize = str2int(optarg);    break;
        case 'f': s_dumpFile = optarg;          break;
        case 'x': s_dumpBytes = str2int(optarg);break;
        case 'v': s_verbose++;                  break;
        case 'c': s_cnt = str2int(optarg);      break;
        case 'l': loopback = true;              break;
        case 'h': showHelp();                   return 0;
        case 'i': gpuIdx = atoi(optarg);        break;
        default:  showHelp();                   return 1;
        }
    }

    TestSession session;
    if (initSession(session, dev.c_str(), gpuIdx, bufCnt, bufSize, loopback) < 0) {
        cleanupSession(session);
        return 1;
    }

    runSimpleLoop(session);

    cleanupSession(session);
    return 0;
}

/**
 * @brief Bring up the FPGA + GPU resources required by the simple-loop test.
 *
 * Sequence:
 *  1. Open /dev/datadev_X via DataGPU (RAII).
 *  2. cuInit + cuDeviceGet + cuCtxCreate via CudaContext.
 *  3. Verify firmware exposes the GpuAsync interface and is V4 or newer.
 *  4. Map the FPGA register block (host-mapped + GPU-mapped) and instantiate
 *     a GpuAsyncCoreRegs view over it.
 *  5. Disable engines, capture dmaHeaderSize, and set the V4
 *     RemoteWriteMaxSize register.
 *  6. cudaMalloc per-buffer rx (and tx, when looping back), each sized
 *     bufSize + dmaHeaderSize for descriptor headroom.
 *  7. Register the buffers with the driver via gpuAddNvidiaMemory.
 *  8. Create a CUDA stream and arm the FPGA free list via cuStreamWriteValue32.
 *  9. Synchronise the stream so all arming writes have landed before the host
 *     enables the FPGA engines.
 *
 * On error returns -1 with diagnostic on stderr; cleanupSession() is safe to
 * call on a partially-initialised session.
 */
static int initSession(TestSession& s, const char* dev, int gpuIdx,
                       int bufCnt, int bufSize, bool loopback) {
    s.bufCnt = bufCnt;
    s.bufSize = bufSize;
    s.loopback = loopback;

    if (bufSize <= 0 || (bufSize % 0x10000) != 0) {
        fprintf(stderr, "bufSize 0x%x must be a positive multiple of 64 KiB\n",
                (unsigned int)bufSize);
        return -1;
    }
    if (bufCnt <= 0) {
        fprintf(stderr, "bufCnt must be positive\n");
        return -1;
    }

    /* CudaContext throws on cuInit failure; let it propagate as a clean exit. */
    if (!s.cuda.init(gpuIdx)) {
        fprintf(stderr, "CUDA context initialization failed\n");
        return -1;
    }

    /* Stream-memory-ops are required for the cuStreamWriteValue32 / cuStreamWaitValue32
     * primitives this test uses. */
    if (!s.cuda.getAttribute(CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_MEM_OPS_V1)) {
        fprintf(stderr, "Selected GPU lacks stream memory ops; aborting\n");
        return -1;
    }

    try {
        s.dataGpu = new DataGPU(dev);
    } catch (...) {
        return -1;
    }
    const int fd = s.dataGpu->fd();

    if (!gpuIsGpuAsyncSupported(fd)) {
        fprintf(stderr, "Firmware or driver does not support GPUAsync\n");
        return -1;
    }

    {
        ssize_t fwVersion = gpuGetGpuAsyncVersion(fd);
        if (fwVersion < 0) {
            fprintf(stderr, "gpuGetGpuAsyncVersion ioctl failed: %s (errno=%d)\n",
                    strerror(errno), errno);
            return -1;
        }
        if (fwVersion < 4) {
            fprintf(stderr, "GpuAsyncCore version %zd < 4 is not supported by rdmaTest\n",
                    fwVersion);
            return -1;
        }
    }

    {
        ssize_t maxBuffers = gpuGetMaxBuffers(fd);
        if (maxBuffers < 0) {
            fprintf(stderr, "gpuGetMaxBuffers ioctl failed: %s (errno=%d)\n",
                    strerror(errno), errno);
            return -1;
        }
        if ((ssize_t)bufCnt > maxBuffers) {
            fprintf(stderr, "Too many buffers requested: %d > %zd\n",
                    bufCnt, maxBuffers);
            return -1;
        }
    }

    if (gpuMapHostFpgaMem(&s.regs, fd, GPU_ASYNC_CORE_OFFSET, GPU_ASYNC_CORE_SIZE) < 0) {
        fprintf(stderr, "Failed to map GpuAsyncCore registers\n");
        return -1;
    }

    s.coreRegs = new GpuAsyncCoreRegs(s.regs.ptr);

    /* Disable engines first; idempotent recovery from a prior crashed run. */
    s.coreRegs->setWriteEnable(0);
    s.coreRegs->setReadEnable(0);

    s.dmaHeaderSize = s.coreRegs->dmaDataBytes();
    s.coreRegs->setRemoteWriteMaxSize(0, bufSize);

    /* Allocate rx (and optionally tx) buffers sized bufSize + dmaHeaderSize for
     * descriptor headroom; only bufSize is registered with the FPGA. */
    s.rxBuffers.resize(bufCnt, nullptr);
    for (int i = 0; i < bufCnt; ++i) {
        if (cudaMalloc(&s.rxBuffers[i], bufSize + s.dmaHeaderSize) != cudaSuccess) {
            fprintf(stderr, "cudaMalloc(rxBuffers[%d]) failed\n", i);
            return -1;
        }
        if (gpuAddNvidiaMemory(fd, 1, (uint64_t)s.rxBuffers[i], bufSize) < 0) {
            fprintf(stderr, "gpuAddNvidiaMemory(rx[%d]) failed: %s\n",
                    i, strerror(errno));
            return -1;
        }
    }
    if (loopback) {
        s.txBuffers.resize(bufCnt, nullptr);
        for (int i = 0; i < bufCnt; ++i) {
            if (cudaMalloc(&s.txBuffers[i], bufSize + s.dmaHeaderSize) != cudaSuccess) {
                fprintf(stderr, "cudaMalloc(txBuffers[%d]) failed\n", i);
                return -1;
            }
            if (gpuAddNvidiaMemory(fd, 0, (uint64_t)s.txBuffers[i], bufSize) < 0) {
                fprintf(stderr, "gpuAddNvidiaMemory(tx[%d]) failed: %s\n",
                        i, strerror(errno));
                return -1;
            }
        }
    }

    if (cudaStreamCreate(&s.stream) != cudaSuccess) {
        fprintf(stderr, "cudaStreamCreate failed\n");
        return -1;
    }

    s.coreRegs->setWriteCount(bufCnt - 1);
    s.coreRegs->setReadCount(bufCnt - 1);

    /* Arm the FPGA free list and pre-clear doorbells via the GPU stream so the
     * writes traverse the same PCIe path as runtime traffic. The host enable
     * below must wait for these to land. */
    for (int i = 0; i < bufCnt; ++i) {
        CUresult cr;
        if ((cr = cuStreamWriteValue32(s.stream,
                  s.regs.dptr + s.coreRegs->freeListOffset(i), 1, 0)) != CUDA_SUCCESS ||
            (cr = cuStreamWriteValue32(s.stream,
                  (CUdeviceptr)s.rxBuffers[i] + 4, 0, 0)) != CUDA_SUCCESS ||
            (loopback &&
             (cr = cuStreamWriteValue32(s.stream,
                  (CUdeviceptr)s.txBuffers[i], 1, 0)) != CUDA_SUCCESS)) {
            const char *en = nullptr;
            cuGetErrorName(cr, &en);
            fprintf(stderr, "cuStreamWriteValue32 (buffer %d) failed: %s\n",
                    i, en ? en : "?");
            return -1;
        }
    }
    if (cuStreamSynchronize(s.stream) != CUDA_SUCCESS) {
        fprintf(stderr, "cuStreamSynchronize after free-list arming failed\n");
        return -1;
    }

    s.coreRegs->setWriteEnable(1);
    if (loopback)
        s.coreRegs->setReadEnable(1);

    return 0;
}

static void cleanupSession(TestSession& s) {
    if (s.coreRegs) {
        s.coreRegs->setWriteEnable(0);
        s.coreRegs->setReadEnable(0);
    }

    if (s.stream) {
        cudaStreamDestroy(s.stream);
        s.stream = 0;
    }

    const int fd = s.dataGpu ? s.dataGpu->fd() : -1;
    if (fd >= 0)
        gpuRemNvidiaMemory(fd);

    for (auto* p : s.txBuffers) if (p) cudaFree(p);
    s.txBuffers.clear();
    for (auto* p : s.rxBuffers) if (p) cudaFree(p);
    s.rxBuffers.clear();

    delete s.coreRegs;
    s.coreRegs = nullptr;

    if (s.regs.ptr || s.regs.dptr)
        gpuUnmapFpgaMem(&s.regs);
    s.regs = {};

    delete s.dataGpu;
    s.dataGpu = nullptr;

    /* CudaContext does not destroy the underlying CUcontext on destruction;
     * doing so on process exit is harmless. */
    if (s.cuda.context())
        cuCtxDestroy(s.cuda.context());
}

/**
 * Run a simple test receiving data from the FPGA, optionally decoding the header or
 * dumping event data to file.
 */
static void runSimpleLoop(TestSession& s) {
    uint64_t totalRecv = 0;
    uint64_t totalEvents = 0, invalidEvents = 0;
    int curBuff = 0;
    const size_t dumpBytes = (s_dumpBytes > 0) ? static_cast<size_t>(s_dumpBytes) : 0U;
    std::vector<uint8_t> tmpbuf(dumpBytes);

    while (s_cnt == -1 || s_cnt-- > 0) {
        AxiWrDesc64_t hdr;

        /* Wait on handshake space on the GPU side (A.K.A. "GPU's doorbell"). */
        assertOk(cuStreamWaitValue32(s.stream,
            (CUdeviceptr)s.rxBuffers[curBuff] + 4, 1, CU_STREAM_WAIT_VALUE_GEQ));

        /* Download header data immediately. */
        assertOk(cudaMemcpyAsync(&hdr, s.rxBuffers[curBuff], sizeof(hdr),
                                 cudaMemcpyDeviceToHost, s.stream));

        /* SYNC the stream so header data becomes available to the host. A failed
         * sync would leave hdr undefined and race the subsequent doorbell writes. */
        assertOk(cuStreamSynchronize(s.stream));

        if (s.loopback) {
            /* Validate hdr.size before the rx->tx device copy. The destination is
             * txBuffers[curBuff] + dmaHeaderSize inside a (bufSize + dmaHeaderSize)
             * allocation, so the safe upper bound is bufSize. A corrupt hdr.size
             * == 0 would trigger a zero-byte copy and a nonsense remoteReadSize
             * doorbell; > bufSize would overrun. Drop the loopback for this event;
             * the rx doorbell clear / free-list refill below still keep the FPGA
             * rx pipeline alive. */
            const uint32_t maxPayload = static_cast<uint32_t>(s.bufSize);
            if (hdr.size == 0 || hdr.size > maxPayload) {
                fprintf(stderr,
                        "Dropping loopback: invalid hdr.size=%u (expected 1..%u)\n",
                        hdr.size, maxPayload);
                invalidEvents++;
            } else {
                /* Wait for the FPGA to return the free list back to GPU. */
                assertOk(cuStreamWaitValue32(s.stream,
                    (CUdeviceptr)s.txBuffers[curBuff], 1, CU_STREAM_WAIT_VALUE_GEQ));

                /* Copy rxData to the txData buffer for this loopback mode at the
                 * dmaDataBytes() payload offset. Use the cached s.dmaHeaderSize
                 * (populated once at init) instead of re-reading the FPGA register
                 * every event. */
                assertOk(cudaMemcpyAsync(
                    s.txBuffers[curBuff] + s.dmaHeaderSize,
                    s.rxBuffers[curBuff] + s.dmaHeaderSize,
                    hdr.size, cudaMemcpyDeviceToDevice, s.stream));

                /* Remove from free list on the GPU side ("GPU's free list"). */
                assertOk(cuStreamWriteValue32(s.stream,
                    (CUdeviceptr)s.txBuffers[curBuff], 0, 0));

                /* SYNC the stream for the data copy and GPU side free list update
                 * before triggering the FPGA's doorbell. */
                assertOk(cuStreamSynchronize(s.stream));

                /* Trigger the FPGA to read txBuffers from the GPU ("FPGA's doorbell"). */
                assertOk(cuStreamWriteValue32(s.stream,
                    s.regs.dptr + s.coreRegs->remoteReadSizeOffset(curBuff),
                    hdr.size, 0));
            }
        }

        /* Clear handshake space on the GPU side ("GPU's doorbell"). */
        assertOk(cuStreamWriteValue32(s.stream,
            (CUdeviceptr)s.rxBuffers[curBuff] + 4, 0, 0));

        /* Return the buffer index back to the FPGA side ("FPGA's free list"). */
        assertOk(cuStreamWriteValue32(s.stream,
            s.regs.dptr + s.coreRegs->freeListOffset(curBuff), 1, 0));

        if (s_verbose > 1) {
            printf("hdr{size=%u, firstUser=%u, lastUser=%u, cont=%u, overflow=%u, result=%u}\n",
                hdr.size, hdr.firstUser, hdr.lastUser, hdr.cont, hdr.overflow, hdr.result);
        }

        /* Dump first N bytes when requested. Clamp the copy size to the
         * actual per-buffer cudaMalloc allocation (bufSize + dmaHeaderSize) so
         * a corrupt hdr.size combined with a large user-supplied -x cannot
         * trigger an out-of-bounds device-to-host copy. */
        if (dumpBytes != 0U) {
            const size_t maxCount = static_cast<size_t>(s.bufSize) +
                                    static_cast<size_t>(s.dmaHeaderSize);
            size_t count = std::min(std::min(static_cast<size_t>(hdr.size),
                                             dumpBytes), maxCount);
            assertOk(cudaMemcpy(tmpbuf.data(), s.rxBuffers[curBuff], count,
                                cudaMemcpyDeviceToHost));
            for (size_t i = 0; i < count; ++i) {
                printf("%02X ", tmpbuf[i]);
                if (i && (i + 1) % 32 == 0)
                    printf("\n");
            }
            printf("\n");
        }

        /* Dump first event to file. */
        if (!s_dumpFile.empty() && !s_dumpToFile) {
            const size_t maxBytes = static_cast<size_t>(s.bufSize) +
                                    static_cast<size_t>(s.dmaHeaderSize);
            if (hdr.size == 0 || hdr.size > maxBytes) {
                fprintf(stderr,
                        "Skipping dump: invalid hdr.size=%u (expected 1..%zu)\n",
                        hdr.size, maxBytes);
            } else {
                std::vector<uint8_t> filebuf(hdr.size);
                assertOk(cudaMemcpy(filebuf.data(), s.rxBuffers[curBuff], hdr.size,
                                    cudaMemcpyDeviceToHost));
                std::ofstream file;
                file.open(s_dumpFile.c_str(), std::ios::binary | std::ios::out);
                if (file.good()) {
                    file.write((char*)filebuf.data(), hdr.size);
                    fprintf(stderr, "Dumped event data to %s\n", s_dumpFile.c_str());
                } else {
                    fprintf(stderr, "Failed to dump event data to %s\n",
                            s_dumpFile.c_str());
                }
                s_dumpToFile = 1;
            }
        }

        totalEvents++;
        totalRecv += (uint64_t)hdr.size;

        curBuff = (curBuff + 1) % s.bufCnt;

        if (totalEvents % 65536 == 0) {
            printf("%-4lu events, %-4lu invalid events, %.2f GiB transferred\n",
                   (unsigned long)totalEvents,
                   (unsigned long)invalidEvents,
                   double(totalRecv) / 1.0E+9);
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
        const char *en = nullptr, *es = nullptr;
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
