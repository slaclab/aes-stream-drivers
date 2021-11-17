/**
 *-----------------------------------------------------------------------------
 * Title      : PGP Read Status Utility
 * ----------------------------------------------------------------------------
 * File       : pgpGetStatus.cpp
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * Utility to read the PGP card status.
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
#include <argp.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <PgpDriver.h>
using namespace std;

const  char * argp_program_version = "pgpGetStatus 1.0";
const  char * argp_program_bug_address = "rherbst@slac.stanford.edu";

struct PrgArgs {
   const char * path;
};

static struct PrgArgs DefArgs = { "/dev/pgpcard_0" };

static struct argp_option options[] = {
   { "path", 'p', "PATH", OPTION_ARG_OPTIONAL, "Path of pgpcard device to use. Default=/dev/pgpcard_0.",0},
   {0}
};

static error_t parseArgs ( int key,  char *arg, struct argp_state *state ) {
   struct PrgArgs *args = (struct PrgArgs *)state->input;

   switch(key) {
      case 'p': args->path = arg; break;
      default: return ARGP_ERR_UNKNOWN; break;
   }
   return(0);
}

static struct argp argp = {options,parseArgs,NULL,NULL};

int main (int argc, char **argv) {
   PgpInfo        info;
   PgpStatus      status;
   PciStatus      pciStatus;
   int            s;
   int            x;
   struct PrgArgs args;

   memcpy(&args,&DefArgs,sizeof(struct PrgArgs));
   argp_parse(&argp,argc,argv,0,0,&args);

   if ( (s = open(args.path, O_RDWR)) <= 0 ) {
      printf("Error opening %s\n",args.path);
      return(1);
   }

   pgpGetInfo(s,&info);
   pgpGetPci(s,&pciStatus);

   printf("-------------- Card Info ------------------\n");
   printf("                 Type : 0x%.2x\n",info.type);
   printf("              Version : 0x%.8x\n",info.version);
   printf("               Serial : 0x%.16lx\n",info.serial);
   printf("           BuildStamp : %s\n",info.buildStamp);
   printf("             LaneMask : 0x%.4x\n",info.laneMask);
   printf("            VcPerMask : 0x%.2x\n",info.vcPerMask);
   printf("              PgpRate : %i\n",info.pgpRate);
   printf("            PromPrgEn : %i\n",info.promPrgEn);

   printf("\n");
   printf("-------------- PCI Info -------------------\n");
   printf("           PciCommand : 0x%.4x\n",pciStatus.pciCommand);
   printf("            PciStatus : 0x%.4x\n",pciStatus.pciStatus);
   printf("          PciDCommand : 0x%.4x\n",pciStatus.pciDCommand);
   printf("           PciDStatus : 0x%.4x\n",pciStatus.pciDStatus);
   printf("          PciLCommand : 0x%.4x\n",pciStatus.pciLCommand);
   printf("           PciLStatus : 0x%.4x\n",pciStatus.pciLStatus);
   printf("         PciLinkState : 0x%x\n",pciStatus.pciLinkState);
   printf("          PciFunction : 0x%x\n",pciStatus.pciFunction);
   printf("            PciDevice : 0x%x\n",pciStatus.pciDevice);
   printf("               PciBus : 0x%.2x\n",pciStatus.pciBus);
   printf("             PciLanes : %i\n",pciStatus.pciLanes);

   for (x=0; x < 8; x++) {
      if ( ((1 << x) & info.laneMask) == 0 ) continue;

      pgpGetStatus(s,x,&status);

      printf("\n");
      printf("-------------- Lane %i --------------------\n",x);

      printf("             LoopBack : %i\n",status.loopBack);
      printf("         LocLinkReady : %i\n",status.locLinkReady);
      printf("         RemLinkReady : %i\n",status.remLinkReady);
      printf("              RxReady : %i\n",status.rxReady);
      printf("              TxReady : %i\n",status.txReady);
      printf("              RxCount : %i\n",status.rxCount);
      printf("           CellErrCnt : %i\n",status.cellErrCnt);
      printf("          LinkDownCnt : %i\n",status.linkDownCnt);
      printf("           LinkErrCnt : %i\n",status.linkErrCnt);
      printf("              FifoErr : %i\n",status.fifoErr);
      printf("              RemData : 0x%.2x\n",status.remData);
      printf("        RemBuffStatus : 0x%.2x\n",status.remBuffStatus);
   }
   close(s);

}

