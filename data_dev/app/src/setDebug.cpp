/**
 *-----------------------------------------------------------------------------
 * Title      : Debug utility
 * ----------------------------------------------------------------------------
 * File       : setDebug.cpp
 * Created    : 2017-03-24
 * ----------------------------------------------------------------------------
 * Description:
 * This program set the driver debug level.
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
#include <argp.h>
#include <stdlib.h>
#include <PgpDriver.h>
using namespace std;

const  char * argp_program_version = "setDebug 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

struct PrgArgs {
   const char * path;
   uint32_t     level;
};

static struct PrgArgs DefArgs = { "/dev/datadev_0", 0x00 };

static char   args_doc[] = "debugLevel";
static char   doc[]      = "\n   Debug level is either 0 or 1.";

static struct argp_option options[] = {
   { "path", 'p', "PATH", OPTION_ARG_OPTIONAL, "Path of pgpcard device to use. Default=/dev/datadev_0.",0},
   {0}
};

error_t parseArgs ( int key,  char *arg, struct argp_state *state ) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch(key) {
      case 'p': args->path = arg; break;
      case ARGP_KEY_ARG: 
          switch (state->arg_num) {
             case 0: args->level = strtol(arg,NULL,10); break;
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

   struct PrgArgs args;

   memcpy(&args,&DefArgs,sizeof(struct PrgArgs));
   argp_parse(&argp,argc,argv,0,0,&args);

   if ( (s = open(args.path, O_RDWR)) <= 0 ) {
      printf("Error opening %s\n",args.path);
      return(1);
   }

   printf("Setting debug level to %i\n",args.level);
   dmaSetDebug(s,args.level);
   close(s);
}

