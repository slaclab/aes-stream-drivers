/**
 *-----------------------------------------------------------------------------
 * Title      : PGP Firmware Update Utility
 * ----------------------------------------------------------------------------
 * File       : pgpPromLoad.cpp
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * Utility to program the PGP card with new firmware.
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

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <linux/types.h>

#include <fcntl.h>
#include <sstream>
#include <string>
#include <iomanip>
#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <argp.h>

#include <PgpDriver.h>
#include "PciCardProm.h"

using namespace std;

const  char * argp_program_version = "pgpPromLoad 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

struct PrgArgs {
   const char * path;
   const char * file;
};

static struct PrgArgs DefArgs = { "/dev/pgpcard_0", "" };

static char   args_doc[] = "promFile";
static char   doc[]      = "\n   PromFile is the appropriate .mcs file for the card.";

static struct argp_option options[] = {
   { "path", 'p', "PATH", OPTION_ARG_OPTIONAL, "Path of pgpcard device to use. Default=/dev/pgpcard_0.",0},
   {0}
};

error_t parseArgs ( int key,  char *arg, struct argp_state *state ) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch(key) {
      case 'p': args->path = arg; break;
      case ARGP_KEY_ARG:
          switch (state->arg_num) {
             case 0: args->file = arg; break;
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
   int fd;
   PciCardProm *prom;
   PgpInfo info;
   struct PrgArgs args;
   bool gen3;

   memcpy(&args,&DefArgs,sizeof(struct PrgArgs));
   argp_parse(&argp,argc,argv,0,0,&args);

   if ( (fd = open(args.path, O_RDWR)) <= 0 ) {
      printf("Error opening %s\n",args.path);
      return(1);
   }

   // Get card info
   pgpGetInfo(fd,&info);
   gen3 = false;

   // Determine version, use only lower 4 bits to get the base version
   switch (info.type & 0xF) {
      case PGP_GEN3: gen3 = true; break;
      case PGP_GEN2:
         if ( info.promPrgEn ) {
            ::close(fd);
            return(-1);
         }
         break;
      default:
         ::close(fd);
         return(-1);
         break;
   }

   // Create the PciCardG3Prom object
   prom = new PciCardProm(fd,args.file,gen3);

   // Check if the .mcs file exists
   if(!prom->fileExist()){
      cout << "Error opening: " << args.file << endl;
      delete prom;
      close(fd);
      return(1);
   }

   // Erase the PROM
   prom->eraseBootProm();

   // Write the .mcs file to the PROM
   if(!prom->writeBootProm()) {
      cout << "Error in prom->bufferedWriteBootProm() function" << endl;
      delete prom;
      close(fd);
      return(1);
   }

   // Compare the .mcs file with the PROM
   if(!prom->verifyBootProm()) {
      cout << "Error in prom->verifyBootProm() function" << endl;
      delete prom;
      close(fd);
      return(1);
   }

   // Display Reminder
   prom->rebootReminder();

	// Close all the devices
   delete prom;
   close(fd);
   return(0);
}
