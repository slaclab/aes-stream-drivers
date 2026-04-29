/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Userspace smoke harness for /dev/nvidia_p2p_stub_mem. Verifies that the
 *    nvidia_p2p_stub miscdevice accepts the STUB_RESERVE_BUF ioctl, mmap()s
 *    the requested buffer, and that a byte pattern written through the mmap VA
 *    is readable back.
 *    Exit codes: 0=PASS, 1=open failed, 2=ioctl failed, 3=mmap failed, 4=pattern mismatch.
 *----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the aes_stream_drivers package, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 *----------------------------------------------------------------------------
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* The shared kernel+uapi header. When compiled without __KERNEL__
 * defined, only the uapi surface (struct stub_reserve_req, STUB_RESERVE_BUF,
 * STUB_FAKE_DMA_* constants) is visible. The kernel-only section
 * (struct emu_gpu_addr_entry, prototypes) is hidden behind #ifdef __KERNEL__. */
#include "emu_gpu_addr_table.h"

#define DEV_PATH    "/dev/nvidia_p2p_stub_mem"
#define TEST_SIZE   65536          /* 64KB = one STUB_PAGE_SIZE */
#define PATTERN_LEN 64
#define PATTERN     0xA5

int main(void)
{
    int fd;
    struct stub_reserve_req req;
    void *mmap_va;
    uint8_t *p;
    uint8_t readback[PATTERN_LEN];
    size_t i;
    off_t offset;
    long page_size;

    fd = open(DEV_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "stub_mmap_test: FAIL open %s: %s\n",
                DEV_PATH, strerror(errno));
        return 1;
    }

    memset(&req, 0, sizeof(req));
    req.size = TEST_SIZE;
    if (ioctl(fd, STUB_RESERVE_BUF, &req) < 0) {
        fprintf(stderr, "stub_mmap_test: FAIL ioctl STUB_RESERVE_BUF: %s\n",
                strerror(errno));
        close(fd);
        return 2;
    }

    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
        page_size = 4096;
    /* kernel receives vm_pgoff = offset/PAGE_SIZE = buf_id */
    offset = (off_t)req.buf_id * page_size;

    mmap_va = mmap(NULL, TEST_SIZE, PROT_READ | PROT_WRITE,
                   MAP_SHARED, fd, offset);
    if (mmap_va == MAP_FAILED) {
        fprintf(stderr, "stub_mmap_test: FAIL mmap offset=0x%lx: %s\n",
                (unsigned long)offset, strerror(errno));
        close(fd);
        return 3;
    }

    /* Write a deterministic pattern. */
    p = (uint8_t *)mmap_va;
    memset(p, PATTERN, PATTERN_LEN);

    /* Read back via a separate memcpy (defensive: ensures the compiler
     * doesn't optimize the write+readback into the same local).  */
    memcpy(readback, p, PATTERN_LEN);

    for (i = 0; i < PATTERN_LEN; i++) {
        if (readback[i] != PATTERN) {
            fprintf(stderr,
                    "stub_mmap_test: FAIL pattern mismatch at byte %zu: "
                    "wrote 0x%02x, read 0x%02x\n",
                    i, PATTERN, readback[i]);
            munmap(mmap_va, TEST_SIZE);
            close(fd);
            return 4;
        }
    }

    if (munmap(mmap_va, TEST_SIZE) < 0) {
        fprintf(stderr, "stub_mmap_test: WARN munmap: %s\n",
                strerror(errno));
        /* non-fatal at this point — pattern already verified */
    }

    close(fd);

    printf("stub_mmap_test: PASS buf_id=%u size=%u pattern-verified\n",
           req.buf_id, TEST_SIZE);
    return 0;
}
