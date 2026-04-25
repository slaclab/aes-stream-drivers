/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    Ioctl sanity test for the datadev driver. Runs 27 numbered checks that
 *    cover 26 non-GPU ioctls exposed by DmaDriver.h, AxisDriver.h, and
 *    AxiVersion.h plus 1 GPU readiness ioctl (GPU_Is_Gpu_Async_Supp) that is
 *    expected to return 0 (CPU build) or 1 (GPU build). Each call is verified
 *    to return a sane value; the program exits 0 if every check passes, 1
 *    otherwise.
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
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <argp.h>
#include <inttypes.h>
#include <errno.h>
#include <iostream>
#include <cstdio>
#include <string>

#include <AxisDriver.h>
#include <AxiVersion.h>
#include <GpuAsync.h>

using std::cout;
using std::endl;

const  char * argp_program_version     = "dmaIoctlTest 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

#ifndef DEFAULT_AXI_DEVICE
#define DEFAULT_AXI_DEVICE "/dev/datadev_0"
#endif

struct PrgArgs {
   const char * path;
};

static struct PrgArgs DefArgs = { DEFAULT_AXI_DEVICE };

static char args_doc[] = "";
static char doc[]      = "Exercise every non-GPU ioctl exposed by the datadev driver and report pass/fail.";

static struct argp_option options[] = {
   { "path", 'p', "PATH", 0, "Path of datadev device to use. Default=" DEFAULT_AXI_DEVICE ".", 0 },
   { 0 }
};

error_t parseArgs(int key, char *arg, struct argp_state *state) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch (key) {
      case 'p':
         args->path = arg;
         break;
      default:
         return ARGP_ERR_UNKNOWN;
   }
   return 0;
}

static struct argp argp = { options, parseArgs, args_doc, doc };

// Counters shared by all checks.
static int gErrors = 0;
static int gPassed = 0;

/**
 * Report a single check outcome. Advances the pass/fail counters and prints
 * a PASS or FAIL line describing what was expected.
 */
static void report(const char *name, bool ok, const char *detail) {
   if (ok) {
      gPassed++;
      printf("PASS: %s %s\n", name, detail);
   } else {
      gErrors++;
      printf("FAIL: %s %s\n", name, detail);
   }
}

int main(int argc, char **argv) {
   struct PrgArgs args;
   int            fd;
   char           detail[128];
   ssize_t        rv;

   memcpy(&args, &DefArgs, sizeof(struct PrgArgs));
   argp_parse(&argp, argc, argv, 0, 0, &args);

   printf("dmaIoctlTest: opening %s\n", args.path);
   if ((fd = open(args.path, O_RDWR)) < 0) {
      printf("FATAL: cannot open %s (errno=%d)\n", args.path, errno);
      return 1;
   }

   // 1. DMA_Get_Buff_Count -- total (tx+rx) buffer count
   rv = dmaGetBuffCount(fd);
   snprintf(detail, sizeof(detail), "= %zd (expected > 0)", rv);
   report("DMA_Get_Buff_Count", rv > 0, detail);

   // 2. DMA_Get_Buff_Size -- cfgSize
   rv = dmaGetBuffSize(fd);
   snprintf(detail, sizeof(detail), "= %zd (expected > 0)", rv);
   report("DMA_Get_Buff_Size", rv > 0, detail);

   // 3. DMA_Set_Debug
   rv = dmaSetDebug(fd, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected == 0)", rv);
   report("DMA_Set_Debug", rv == 0, detail);

   // 4. DMA_Set_Mask -- set dest 0 via 32-bit mask convenience wrapper
   rv = dmaSetMask(fd, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected == 0)", rv);
   report("DMA_Set_Mask", rv == 0, detail);

   // 5. DMA_Get_Index + DMA_Ret_Index round-trip
   int32_t idx = dmaGetIndex(fd);
   snprintf(detail, sizeof(detail), "= %d (expected >= 0)", idx);
   report("DMA_Get_Index", idx >= 0, detail);
   if (idx >= 0) {
      ssize_t rret = dmaRetIndex(fd, idx);
      snprintf(detail, sizeof(detail), "idx=%d ret=%zd (expected == 0)", idx, rret);
      report("DMA_Ret_Index", rret == 0, detail);
   } else {
      // Cannot test DMA_Ret_Index without a valid index; log as skipped.
      printf("SKIP: DMA_Ret_Index (no valid index to return)\n");
   }

   // 6. DMA_Read_Ready -- 0 (no data) or 1 (data pending) are both acceptable
   rv = dmaReadReady(fd);
   snprintf(detail, sizeof(detail), "= %zd (expected >= 0)", rv);
   report("DMA_Read_Ready", rv >= 0, detail);

   // 7. DMA_Set_MaskBytes -- 512-byte bitmap, dest 0 only
   uint8_t mask[DMA_MASK_SIZE];
   dmaInitMaskBytes(mask);
   dmaAddMaskBytes(mask, 0);
   rv = dmaSetMaskBytes(fd, mask);
   snprintf(detail, sizeof(detail), "= %zd (expected == 0)", rv);
   report("DMA_Set_MaskBytes", rv == 0, detail);

   // 8. DMA_Get_Version
   rv = dmaGetApiVersion(fd);
   snprintf(detail, sizeof(detail), "= 0x%zx (expected 0x%x)", rv, DMA_VERSION);
   report("DMA_Get_Version", rv == DMA_VERSION, detail);

   // 9. DMA_Write_Register -- address inside configured PHY region
   rv = dmaWriteRegister(fd, 0x00010000, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected == 0)", rv);
   report("DMA_Write_Register", rv == 0, detail);

   // 10. DMA_Read_Register
   uint32_t regVal = 0;
   rv = dmaReadRegister(fd, 0x00010000, &regVal);
   snprintf(detail, sizeof(detail), "= %zd data=0x%x (expected >= 0)", rv, regVal);
   report("DMA_Read_Register", rv >= 0, detail);

   // 11. DMA_Get_RxBuff_Count
   rv = dmaGetRxBuffCount(fd);
   snprintf(detail, sizeof(detail), "= %zd (expected > 0)", rv);
   report("DMA_Get_RxBuff_Count", rv > 0, detail);

   // 12. DMA_Get_TxBuff_Count
   rv = dmaGetTxBuffCount(fd);
   snprintf(detail, sizeof(detail), "= %zd (expected > 0)", rv);
   report("DMA_Get_TxBuff_Count", rv > 0, detail);

   // 13. DMA_Get_TxBuffinUser_Count
   rv = ioctl(fd, DMA_Get_TxBuffinUser_Count, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected >= 0)", rv);
   report("DMA_Get_TxBuffinUser_Count", rv >= 0, detail);

   // 14. DMA_Get_TxBuffinHW_Count
   rv = ioctl(fd, DMA_Get_TxBuffinHW_Count, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected >= 0)", rv);
   report("DMA_Get_TxBuffinHW_Count", rv >= 0, detail);

   // 15. DMA_Get_TxBuffinPreHWQ_Count
   rv = ioctl(fd, DMA_Get_TxBuffinPreHWQ_Count, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected >= 0)", rv);
   report("DMA_Get_TxBuffinPreHWQ_Count", rv >= 0, detail);

   // 16. DMA_Get_TxBuffinSWQ_Count
   rv = ioctl(fd, DMA_Get_TxBuffinSWQ_Count, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected >= 0)", rv);
   report("DMA_Get_TxBuffinSWQ_Count", rv >= 0, detail);

   // 17. DMA_Get_TxBuffMiss_Count
   rv = ioctl(fd, DMA_Get_TxBuffMiss_Count, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected >= 0)", rv);
   report("DMA_Get_TxBuffMiss_Count", rv >= 0, detail);

   // 18. DMA_Get_RxBuffinUser_Count
   rv = ioctl(fd, DMA_Get_RxBuffinUser_Count, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected >= 0)", rv);
   report("DMA_Get_RxBuffinUser_Count", rv >= 0, detail);

   // 19. DMA_Get_RxBuffinHW_Count
   rv = ioctl(fd, DMA_Get_RxBuffinHW_Count, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected >= 0)", rv);
   report("DMA_Get_RxBuffinHW_Count", rv >= 0, detail);

   // 20. DMA_Get_RxBuffinPreHWQ_Count
   rv = ioctl(fd, DMA_Get_RxBuffinPreHWQ_Count, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected >= 0)", rv);
   report("DMA_Get_RxBuffinPreHWQ_Count", rv >= 0, detail);

   // 21. DMA_Get_RxBuffinSWQ_Count
   rv = ioctl(fd, DMA_Get_RxBuffinSWQ_Count, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected >= 0)", rv);
   report("DMA_Get_RxBuffinSWQ_Count", rv >= 0, detail);

   // 22. DMA_Get_RxBuffMiss_Count
   rv = ioctl(fd, DMA_Get_RxBuffMiss_Count, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected >= 0)", rv);
   report("DMA_Get_RxBuffMiss_Count", rv >= 0, detail);

   // 23. DMA_Get_GITV -- driver git version string
   std::string gitv = dmaGetGitVersion(fd);
   snprintf(detail, sizeof(detail), "= '%s' (expected non-empty)", gitv.c_str());
   report("DMA_Get_GITV", !gitv.empty(), detail);

   // 24. AXIS_Read_Ack -- void return, just verify no crash
   axisReadAck(fd);
   gPassed++;
   printf("PASS: AXIS_Read_Ack (void -- no crash)\n");

   // 25. AXIS_Write_ReqMissed
   rv = ioctl(fd, AXIS_Write_ReqMissed, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected >= 0)", rv);
   report("AXIS_Write_ReqMissed", rv >= 0, detail);

   // 26. AVER_Get -- AxiVersion struct fill
   struct AxiVersion aVer;
   memset(&aVer, 0, sizeof(aVer));
   rv = axiVersionGet(fd, &aVer);
   snprintf(detail, sizeof(detail), "= %zd fwVer=0x%x (expected == 0)", rv, aVer.firmwareVersion);
   report("AVER_Get", rv == 0, detail);

   // 27. GPU_Is_Gpu_Async_Supp -- 0 (CPU build) or 1 (GPU build) are both valid
   rv = ioctl(fd, GPU_Is_Gpu_Async_Supp, 0);
   snprintf(detail, sizeof(detail), "= %zd (expected 0 or 1)", rv);
   report("GPU_Is_Gpu_Async_Supp", rv == 0 || rv == 1, detail);

   printf("\nIoctl test: %d passed, %d failed\n", gPassed, gErrors);

   close(fd);
   return gErrors > 0 ? 1 : 0;
}
