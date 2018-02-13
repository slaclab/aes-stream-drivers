/**
 *-----------------------------------------------------------------------------
 * Title      : AXI Version Access
 * ----------------------------------------------------------------------------
 * File       : axi_version.h
 * Created    : 2017-03-16
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
#include <axi_version.h>
#include <AxiVersion.h>
#include <dma_common.h>
#include <linux/seq_file.h>

// AXI Version Get
int32_t AxiVersion_Get(struct DmaDevice *dev, void * base, uint64_t arg) {
   struct AxiVersion axiVersion;
   int32_t ret;

   AxiVersion_Read(dev, base, &axiVersion);

   if ((ret=copy_to_user((void *)arg,&axiVersion,sizeof(struct AxiVersion)))) {
      dev_warn(dev->device,"AxiVersion_Get: copy_to_user failed. ret=%i, user=%p kern=%p\n",
          ret, (void *)arg, &axiVersion);
      return(-1);
   }
   return(0);
}

// AXI Version Read
void AxiVersion_Read(struct DmaDevice *dev, void * base, struct AxiVersion *aVer) {
   struct AxiVersion_Reg *reg = (struct AxiVersion_Reg *)base;
   uint32_t x;

   aVer->firmwareVersion = ioread32(&(reg->firmwareVersion));
   aVer->scratchPad      = ioread32(&(reg->scratchPad));
   aVer->upTimeCount     = ioread32(&(reg->upTimeCount));
   
   for (x=0; x < 2; x++) 
      ((uint32_t *)aVer->fdValue)[x] = ioread32(&(reg->fdValue[x]));   

   for (x=0; x < 64; x++) 
      aVer->userValues[x] = ioread32(&(reg->userValues[x]));

   aVer->deviceId = ioread32(&(reg->deviceId));

   for (x=0; x < 64; x++) 
      ((uint32_t *)aVer->gitHash)[x] = ioread32(&(reg->gitHash[x]));

   for (x=0; x < 4; x++) 
      ((uint32_t *)aVer->dnaValue)[x] = ioread32(&(reg->dnaValue[x]));

   for (x=0; x < 64; x++) 
      ((uint32_t *)aVer->buildString)[x] = ioread32(&(reg->buildString[x]));
}

// AXI Version Show
void AxiVersion_Show(struct seq_file *s, struct DmaDevice *dev, struct AxiVersion *aVer) {
   int32_t x;

   seq_printf(s,"-------------- Axi Version ----------------\n");
   seq_printf(s,"     Firmware Version : 0x%x\n",aVer->firmwareVersion);
   seq_printf(s,"           ScratchPad : 0x%x\n",aVer->scratchPad);
   seq_printf(s,"        Up Time Count : %u\n",aVer->upTimeCount);
   
   seq_printf(s,"             Fd Value : 0x");
   for (x=0; x < 8; x++) seq_printf(s,"%.02x",aVer->fdValue[x]);
   seq_printf(s,"\n");   
   

   //for (x=0; x < 64; x++)
   //   seq_printf(s,"          User Values : 0x%x\n",aVer->userValues[x]);

   seq_printf(s,"            Device ID : 0x%x\n",aVer->deviceId);

   seq_printf(s,"             Git Hash : ");
   for (x=0; x < 20; x++) seq_printf(s,"%.02x",aVer->gitHash[19-x]);
   seq_printf(s,"\n");

   seq_printf(s,"            DNA Value : 0x");
   for (x=0; x < 16; x++) seq_printf(s,"%.02x",aVer->dnaValue[x]);
   seq_printf(s,"\n");

   seq_printf(s,"         Build String : %s\n",aVer->buildString);
}

