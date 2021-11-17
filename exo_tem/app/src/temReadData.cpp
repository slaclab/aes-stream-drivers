/**
 *-----------------------------------------------------------------------------
 * Title      : TEM read data utility
 * ----------------------------------------------------------------------------
 * File       : temReadData.cpp
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * This program will open up a tem card port and attempt to read data.
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
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <argp.h>
#include <TemDriver.h>
#include <PrbsData.h>
using namespace std;

const  char * argp_program_version = "temRead 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

struct PrgArgs {
   const char * path;
   uint32_t     prbsDis;
};

static struct PrgArgs DefArgs = { "/dev/temcard_0", 0x0 };

static char   args_doc[] = "";
static char   doc[]      = "";

static struct argp_option options[] = {
   { "path",    'p', "PATH",   OPTION_ARG_OPTIONAL, "Path of temcard device to use. Default=/dev/temcard_0.",0},
   { "prbsdis", 'd', 0,        OPTION_ARG_OPTIONAL, "Disable PRBS checking.",0},
   {0}
};

error_t parseArgs ( int key,  char *arg, struct argp_state *state ) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch(key) {
      case 'p': args->path = arg; break;
      case 'd': args->prbsDis = 1; break;
      default: return ARGP_ERR_UNKNOWN; break;
   }
   return(0);
}

static struct argp argp = {options,parseArgs,args_doc,doc};

int main (int argc, char **argv) {
   int32_t       s;
   int32_t       ret;
   int32_t       count;
   fd_set        fds;
   void *        rxData;
   uint32_t      maxSize;
   PrbsData      prbs(32,4,1,2,6,31);
   bool          prbRes;

   struct PrgArgs args;

   struct timeval timeout;

   memcpy(&args,&DefArgs,sizeof(struct PrgArgs));
   argp_parse(&argp,argc,argv,0,0,&args);

   if ( (s = open(args.path, O_RDWR)) <= 0 ) {
      printf("Error opening %s\n",args.path);
      return(1);
   }
   temEnableDataRead(s);

   maxSize = 1024*1024*2;

   if ((rxData = malloc(maxSize)) == NULL ) {
      printf("Failed to allocate rxData!\n");
      return(0);
   }

   count  = 0;
   prbRes = 0;
   do {

      // Setup fds for select call
      FD_ZERO(&fds);
      FD_SET(s,&fds);

      // Setup select timeout for 1 second
      timeout.tv_sec=2;
      timeout.tv_usec=0;

      // Wait for Socket data ready
      ret = select(s+1,&fds,NULL,NULL,&timeout);
      if ( ret <= 0 ) {
         printf("Read timeout\n");
      }
      else {

         ret = temRead(s,rxData,maxSize);

         if ( ret < 0 ) printf("Read error. ret=%i\n",ret);
         else if ( ret > 0 ) {
            if ( args.prbsDis == 0 ) prbRes = prbs.processData(rxData,ret);

            count++;
            printf("Read ret=%i, prbs=%i, count=%i\n",ret,prbRes,count);
         }
      }
   } while ( 1 );

   free(rxData);

   close(s);
   return(0);
}

