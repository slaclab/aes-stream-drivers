/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    This program sets the driver debug level.
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <termios.h>
#include <fcntl.h>
#include <string.h>
#include <argp.h>
#include <stdlib.h>
#include <iostream>
#include <cstdio>

#include <DmaDriver.h>

using std::cout;
using std::endl;

// Version and contact information
const char *argp_program_version = "setDebug 1.0";
const char *argp_program_bug_address = "rherbst@slac.stanford.edu";

// Struct to hold command-line arguments
struct PrgArgs {
   const char *path;
   uint32_t level;
};

// Default command-line arguments
static struct PrgArgs DefArgs = { "/dev/datadev_0", 0x00 };

// Documentation for arguments
static char args_doc[] = "debugLevel";
static char doc[] = "\n   Debug level is either 0 or 1.";

// Option descriptions
static struct argp_option options[] = {
   { "path", 'p', "PATH", OPTION_ARG_OPTIONAL, "Path of datadev device to use. Default=/dev/datadev_0.", 0 },
   { 0 }
};

// Parse a single option
error_t parseArgs(int key, char *arg, struct argp_state *state) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch (key) {
      case 'p':
         args->path = arg;
         break;
      case ARGP_KEY_ARG:
         if (state->arg_num == 0) {
            args->level = strtol(arg, NULL, 10);
         } else {
            argp_usage(state);  // Too many arguments
         }
         break;
      case ARGP_KEY_END:
         if (state->arg_num < 1) {
            argp_usage(state);  // Not enough arguments
         }
         break;
      default:
         return ARGP_ERR_UNKNOWN;
   }
   return 0;
}

// argp parser
static struct argp argp = { options, parseArgs, args_doc, doc };

int main(int argc, char **argv) {
   int s;

   // Initialize argument structure with default values
   struct PrgArgs args;
   memcpy(&args, &DefArgs, sizeof(struct PrgArgs));

   // Parse command-line arguments
   argp_parse(&argp, argc, argv, 0, 0, &args);

   // Open device
   if ((s = open(args.path, O_RDWR)) <= 0) {
      printf("Error opening %s\n", args.path);
      return 1;
   }

   // Set debug level
   printf("Setting debug level to %i\n", args.level);
   dmaSetDebug(s, args.level);

   // Close device
   close(s);

   return 0;
}
