/**
 *-----------------------------------------------------------------------------
 * Title      : PGP sideband data utility
 * ----------------------------------------------------------------------------
 * File       : pgpSetData.cpp
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * This program set the PGP card sideband data for a lane.
 * ----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
    * https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the aes_stream_drivers package, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 * ----------------------------------------------------------------------------
**/

#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <termios.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <argp.h>
#include <PgpDriver.h>
using namespace std;

const  char * argp_program_version = "pgpSetData 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

struct PrgArgs {
   const char * path;
   uint32_t     lane;
   uint32_t     data;
};

static struct PrgArgs DefArgs = { "/dev/pgpcard_0", 0x0, 0x00 };

static char   args_doc[] = "data";
static char   doc[]      = "\n   data is passed as a hex value. i.e. 0xAB.";

static struct argp_option options[] = {
   { "path", 'p', "PATH", OPTION_ARG_OPTIONAL, "Path of pgpcard device to use. Default=/dev/pgpcard_0.",0},
   { "lane", 'l', "MASK", OPTION_ARG_OPTIONAL, "Mask of lanes to set. 1 bit per lane in hex. i.e. 0xFF.",0},
   {0}
};

error_t parseArgs ( int key,  char *arg, struct argp_state *state ) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch(key) {
      case 'p': args->path = arg; break;
      case 'l': args->lane = strtol(arg,NULL,16); break;
      case ARGP_KEY_ARG:
          switch (state->arg_num) {
             case 0: args->data = strtol(arg,NULL,16); break;
             default: argp_usage(state); break;
          }
          break;
      case ARGP_KEY_END:
          if ( state->arg_num < 1) argp_usage(state);
          break;
      default: return ARGP_ERR_UNKNOWN; break;
   }
   return(0);
}

static struct argp argp = {options,parseArgs,args_doc,doc};

int main (int argc, char **argv) {
   int s;
   uint32_t x;
   struct PrgArgs args;
   struct PgpInfo info;

   memcpy(&args,&DefArgs,sizeof(struct PrgArgs));
   argp_parse(&argp,argc,argv,0,0,&args);

   if ( (s = open(args.path, O_RDWR)) <= 0 ) {
      printf("Error opening %s\n",args.path);
      return(1);
   }
   pgpGetInfo(s,&info);

   for (x=0; x < 8; x++) {
      if ( ((1 << x) & info.laneMask) != 0 ) {
         printf("Setting lane %i data to 0x%.2x\n",x,args.data);
         pgpSetData(s,x,args.data);
      }
   }
   close(s);
}

