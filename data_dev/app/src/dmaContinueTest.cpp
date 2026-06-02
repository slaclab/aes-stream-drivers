/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 * Exercises the DMA "continue" feature: a logical frame that the hardware
 * (or the CI emulator) splits across multiple DMA buffers, setting the
 * continue (cont) flag on every buffer except the last. The kernel driver
 * does NOT reassemble continued frames — it hands each cont-flagged buffer
 * to user space — so this utility stitches the segments back together by the
 * cont flag before validating the payload with PRBS.
 *
 * It sends one PRBS frame at a time (cont=0 on TX so the device performs the
 * split), then drains RX buffers into a contiguous assembly buffer until it
 * sees cont=0, and runs PrbsData::processData() on the reassembled frame.
 * Each RX buffer is copied out via read() and returned to the pool
 * immediately, so a single frame never pins more than one DMA buffer.
 *
 * With the emulator's emu_max_transfer set below the frame size (see
 * tests/test_continue_frame.sh), every frame crosses the continue boundary
 * and the reassembly path is fully exercised. A clean run prints
 * "=== dmaContinueTest: PASS ===" and exits 0; any framing or PRBS error
 * prints a "Read Error" / "Prbs mismatch" line (matched by the CI gate) and
 * exits non-zero.
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
#include <linux/types.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <argp.h>

#include <AxisDriver.h>
#include <PrbsData.h>

const char * argp_program_version     = "dmaContinueTest 1.0";
const char * argp_program_bug_address  = "ruckman@slac.stanford.edu";

#ifndef DEFAULT_AXI_DEVICE
#define DEFAULT_AXI_DEVICE "/dev/datadev_0"
#endif

/* Per-frame wall-clock budget. One segment is delivered per ~1ms emulator
 * poll cycle, so even a large multi-segment frame completes well inside this. */
#define FRAME_TIMEOUT_SEC 5

struct PrgArgs {
   const char * path;
   uint32_t     dest;
   uint32_t     size;
   uint32_t     count;
   uint32_t     fuser;
   uint32_t     luser;
   uint32_t     prbsDis;
};

static struct PrgArgs DefArgs = { DEFAULT_AXI_DEVICE, 0, 65536, 100, 0x2, 0x0, 0 };

static char args_doc[] = "";
static char doc[]      =
   "Validate the DMA continue feature: send PRBS frames, reassemble the "
   "cont-flagged RX segments, and check payload integrity.";

static struct argp_option options[] = {
   { "path",    'p', "PATH",  0, "Device to use. Default=" DEFAULT_AXI_DEVICE ".", 0},
   { "dest",    'm', "DEST",  0, "Destination channel. Default=0", 0},
   { "size",    's', "SIZE",  0, "Logical frame size in bytes. Default=65536", 0},
   { "count",   'c', "COUNT", 0, "Number of frames to send. Default=100", 0},
   { "fuser",   'f', "FUSER", 0, "First user field in hex. Default=0x2", 0},
   { "luser",   'l', "LUSER", 0, "Last user field in hex. Default=0x0", 0},
   { "prbsdis", 'd', 0,       0, "Disable PRBS checking.", 0},
   {0}
};

static error_t parseArgs(int key, char *arg, struct argp_state *state) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch (key) {
      case 'p': args->path    = arg;                       break;
      case 'm': args->dest    = strtol(arg, NULL, 10);     break;
      case 's': args->size    = strtol(arg, NULL, 10);     break;
      case 'c': args->count   = strtol(arg, NULL, 10);     break;
      case 'f': args->fuser   = strtol(arg, NULL, 16);     break;
      case 'l': args->luser   = strtol(arg, NULL, 16);     break;
      case 'd': args->prbsDis = 1;                         break;
      default:  return ARGP_ERR_UNKNOWN;                   break;
   }
   return 0;
}

static struct argp argp = {options, parseArgs, args_doc, doc};

/* Monotonic seconds, for per-frame deadlines. */
static double monoNow(void) {
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Send one frame (cont=0; the device splits it). Returns true on success. */
static bool sendFrame(int fd, const void *buf, uint32_t size,
                      uint32_t fuser, uint32_t luser, uint32_t dest) {
   fd_set         fds;
   struct timeval tv;
   double         deadline = monoNow() + FRAME_TIMEOUT_SEC;

   while (monoNow() < deadline) {
      FD_ZERO(&fds);
      FD_SET(fd, &fds);
      tv.tv_sec  = 0;
      tv.tv_usec = 1000;
      if (select(fd + 1, NULL, &fds, NULL, &tv) <= 0) continue;

      ssize_t ret = dmaWrite(fd, buf, size, axisSetFlags(fuser, luser, 0), dest);
      if (ret < 0) {
         printf("Write Error: dmaWrite returned %li\n", ret);
         return false;
      }
      if (ret > 0) return true;
   }
   printf("Write Error: timed out waiting to send frame\n");
   return false;
}

/*
 * Reassemble one logical frame from its cont-flagged RX segments into asm.
 * Returns the reassembled byte count, or -1 on error (string already
 * printed). segsOut receives the number of segments observed.
 */
static ssize_t recvFrame(int fd, uint8_t *asmBuf, uint32_t cap,
                         uint8_t *segBuf, uint32_t *segsOut) {
   fd_set         fds;
   struct timeval tv;
   uint32_t       asmOff = 0;
   uint32_t       segs   = 0;
   uint32_t       cont   = 1;
   double         deadline = monoNow() + FRAME_TIMEOUT_SEC;

   while (cont && monoNow() < deadline) {
      FD_ZERO(&fds);
      FD_SET(fd, &fds);
      tv.tv_sec  = 0;
      tv.tv_usec = 1000;
      if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0) continue;

      uint32_t flags = 0;
      uint32_t dest  = 0;
      ssize_t  ret   = dmaRead(fd, segBuf, cap, &flags, NULL, &dest);
      if (ret < 0) {
         printf("Read Error: dmaRead returned %li\n", ret);
         return -1;
      }
      if (ret == 0) continue;

      if (asmOff + (uint32_t)ret > cap) {
         printf("Read Error: reassembled frame exceeds %u bytes (seg=%u)\n", cap, segs);
         return -1;
      }
      memcpy(asmBuf + asmOff, segBuf, ret);
      asmOff += (uint32_t)ret;
      cont    = axisGetCont(flags);
      segs++;
   }

   if (cont) {
      printf("Read Error: continue frame did not terminate (got %u bytes in %u segs)\n",
             asmOff, segs);
      return -1;
   }

   *segsOut = segs;
   return (ssize_t)asmOff;
}

int main(int argc, char **argv) {
   struct PrgArgs args;
   memcpy(&args, &DefArgs, sizeof(struct PrgArgs));
   argp_parse(&argp, argc, argv, 0, 0, &args);

   int fd = open(args.path, O_RDWR);
   if (fd < 0) {
      printf("Error opening device %s\n", args.path);
      return 1;
   }

   uint8_t mask[DMA_MASK_SIZE] = {};
   dmaInitMaskBytes(mask);
   dmaAddMaskBytes(mask, args.dest);
   if (dmaSetMaskBytes(fd, mask) != 0) {
      printf("Error setting dest mask for dest=%u\n", args.dest);
      close(fd);
      return 1;
   }

   /* size is the upper bound for the whole frame and for any single segment. */
   uint8_t *txBuf  = (uint8_t *)malloc(args.size);
   uint8_t *asmBuf = (uint8_t *)malloc(args.size);
   uint8_t *segBuf = (uint8_t *)malloc(args.size);
   if (txBuf == NULL || asmBuf == NULL || segBuf == NULL) {
      printf("Error allocating buffers\n");
      free(txBuf); free(asmBuf); free(segBuf);
      close(fd);
      return 1;
   }

   /* Separate generator/checker instances: both are stateful and must stay
    * in lockstep, one logical frame at a time. */
   PrbsData txPrbs(32, 4, 1, 2, 6, 31);
   PrbsData rxPrbs(32, 4, 1, 2, 6, 31);

   printf("dmaContinueTest: dest=%u size=%u count=%u prbs=%s\n",
          args.dest, args.size, args.count, args.prbsDis ? "off" : "on");

   uint32_t txCount = 0;
   uint32_t rxCount = 0;
   uint32_t prbErr  = 0;
   uint32_t maxSegs = 0;
   bool     fatal   = false;

   for (uint32_t f = 0; f < args.count && !fatal; f++) {
      txPrbs.genData(txBuf, args.size);

      if (!sendFrame(fd, txBuf, args.size, args.fuser, args.luser, args.dest)) {
         fatal = true;
         break;
      }
      txCount++;

      uint32_t segs = 0;
      ssize_t  got  = recvFrame(fd, asmBuf, args.size, segBuf, &segs);
      if (got < 0) {
         fatal = true;
         break;
      }
      rxCount++;
      if (segs > maxSegs) maxSegs = segs;

      if ((uint32_t)got != args.size) {
         printf("Read Error: frame %u reassembled to %li bytes, expected %u\n",
                f, got, args.size);
         fatal = true;
         break;
      }

      if (!args.prbsDis && !rxPrbs.processData(asmBuf, got)) {
         prbErr++;
         printf("Prbs mismatch. frame=%u, dest=%u, segs=%u\n", f, args.dest, segs);
      }
   }

   printf("TxCount: %u\n", txCount);
   printf("RxCount: %u\n", rxCount);
   printf("PrbErr:  %u\n", prbErr);
   printf("MaxSegsPerFrame: %u\n", maxSegs);

   free(txBuf); free(asmBuf); free(segBuf);
   close(fd);

   bool pass = (!fatal) && (txCount == args.count) &&
               (rxCount == args.count) && (prbErr == 0);
   printf("=== dmaContinueTest: %s ===\n", pass ? "PASS" : "FAIL");
   return pass ? 0 : 1;
}
