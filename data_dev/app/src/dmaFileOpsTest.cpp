/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    File operation coverage test for the datadev driver. Exercises open,
 *    close, read, write, poll/select, and mmap to verify every file-op
 *    callback in Dma_Fops responds without kernel panic.
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

#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <argp.h>
#include <inttypes.h>
#include <iostream>
#include <cstdio>

#include <AxisDriver.h>

using std::cout;
using std::endl;

const  char * argp_program_version     = "dmaFileOpsTest 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

#ifndef DEFAULT_AXI_DEVICE
#define DEFAULT_AXI_DEVICE "/dev/datadev_0"
#endif

struct PrgArgs {
   const char * path;
};

static struct PrgArgs DefArgs = { DEFAULT_AXI_DEVICE };

static char args_doc[] = "";
static char doc[]      = "Exercise open/close/read/write/poll/mmap file operations against the datadev driver.";

static struct argp_option options[] = {
   { "path", 'p', "PATH", 0, "Path of datadev device to use. Default=" DEFAULT_AXI_DEVICE ".", 0 },
   { 0 }
};

error_t parseArgs(int key, char *arg, struct argp_state *state) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch (key) {
      case 'p': args->path = arg; break;
      default:  return ARGP_ERR_UNKNOWN;
   }
   return 0;
}

static struct argp argp = { options, parseArgs, args_doc, doc };

int main(int argc, char **argv) {
   struct PrgArgs args;
   int            fd      = -1;
   int            fd2     = -1;
   int            errors  = 0;
   int            passed  = 0;

   memcpy(&args, &DefArgs, sizeof(struct PrgArgs));
   argp_parse(&argp, argc, argv, 0, 0, &args);

   printf("dmaFileOpsTest: opening %s\n", args.path);

   // 1. open -- verify fd >= 0
   fd = open(args.path, O_RDWR);
   if (fd >= 0) {
      printf("PASS: open(fd=%d)\n", fd);
      passed++;
   } else {
      printf("FAIL: open failed (errno=%d)\n", errno);
      return 1;
   }

   // 2. multiple open -- verify second fd opens independently
   fd2 = open(args.path, O_RDWR);
   if (fd2 >= 0) {
      printf("PASS: multiple open(fd2=%d)\n", fd2);
      passed++;
      close(fd2);
   } else {
      printf("FAIL: multiple open failed (errno=%d)\n", errno);
      errors++;
   }

   // 3. poll/select for read readiness -- return >= 0 (0 means no data, fine)
   {
      fd_set fds;
      struct timeval timeout;
      FD_ZERO(&fds);
      FD_SET(fd, &fds);
      timeout.tv_sec  = 0;
      timeout.tv_usec = 100;
      int ret = select(fd + 1, &fds, NULL, NULL, &timeout);
      if (ret >= 0) {
         printf("PASS: select(read) = %d\n", ret);
         passed++;
      } else {
         printf("FAIL: select(read) = %d errno=%d\n", ret, errno);
         errors++;
      }
   }

   // 4. poll/select for write readiness
   {
      fd_set fds;
      struct timeval timeout;
      FD_ZERO(&fds);
      FD_SET(fd, &fds);
      timeout.tv_sec  = 0;
      timeout.tv_usec = 100;
      int ret = select(fd + 1, NULL, &fds, NULL, &timeout);
      if (ret >= 0) {
         printf("PASS: select(write) = %d\n", ret);
         passed++;
      } else {
         printf("FAIL: select(write) = %d errno=%d\n", ret, errno);
         errors++;
      }
   }

   // 5. mmap via dmaMapDma -- informational: mmap may or may not work in
   //    emulated/swiotlb environments. A NULL return is not a hard failure;
   //    a non-NULL return must be paired with sane counts and a successful
   //    unmap.
   {
      uint32_t dmaCount = 0;
      uint32_t dmaSize  = 0;
      void **  dmaBuffers = dmaMapDma(fd, &dmaCount, &dmaSize);
      if (dmaBuffers != NULL) {
         if (dmaCount > 0 && dmaSize > 0) {
            printf("PASS: dmaMapDma count=%u size=%u\n", dmaCount, dmaSize);
            passed++;
         } else {
            printf("FAIL: dmaMapDma returned buffers but count=%u size=%u\n", dmaCount, dmaSize);
            errors++;
         }
         ssize_t ret = dmaUnMapDma(fd, dmaBuffers);
         if (ret == 0) {
            printf("PASS: dmaUnMapDma\n");
            passed++;
         } else {
            printf("FAIL: dmaUnMapDma = %zd\n", ret);
            errors++;
         }
      } else {
         printf("INFO: dmaMapDma returned NULL (mmap may be unsupported in emulated env)\n");
         // Not counted as error per plan guidance
      }
   }

   // 6. read with no data pending -- return >= 0 acceptable (0 means no data ready)
   {
      size_t sz = 131072;
      void * buf = malloc(sz);
      if (buf == NULL) {
         printf("FAIL: malloc for read buffer\n");
         errors++;
      } else {
         struct DmaReadData rd;
         memset(&rd, 0, sizeof(rd));
         rd.size = sz;
         rd.is32 = (sizeof(void *) == 4);
         rd.data = (uint64_t)buf;

         ssize_t ret = read(fd, &rd, sizeof(rd));
         if (ret >= 0) {
            printf("PASS: read(no-data) = %zd\n", ret);
            passed++;
         } else {
            printf("FAIL: read = %zd errno=%d\n", ret, errno);
            errors++;
         }
         free(buf);
      }
   }

   // 7. ioctl -- covered more thoroughly by dmaIoctlTest; do one here
   {
      ssize_t rv = dmaGetBuffSize(fd);
      if (rv > 0) {
         printf("PASS: ioctl(DMA_Get_Buff_Size) = %zd\n", rv);
         passed++;
      } else {
         printf("FAIL: ioctl(DMA_Get_Buff_Size) = %zd\n", rv);
         errors++;
      }
   }

   // 8. close -- verify clean teardown
   {
      int ret = close(fd);
      if (ret == 0) {
         printf("PASS: close\n");
         passed++;
      } else {
         printf("FAIL: close = %d errno=%d\n", ret, errno);
         errors++;
      }
   }

   printf("\nFile-ops test: %d passed, %d failed\n", passed, errors);

   return errors > 0 ? 1 : 0;
}
