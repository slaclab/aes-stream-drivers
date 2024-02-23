/**
 *-----------------------------------------------------------------------------
 * Title      : Common FPGA Prom Functions
 * ----------------------------------------------------------------------------
 * File       : fpga_prom.c
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

#include <fpga_prom.h>
#include <FpgaProm.h>
#include <dma_common.h>

// Prom Read
int32_t FpgaProm_Write(struct DmaDevice *dev, void * base, uint64_t arg) {
   int32_t  ret;
   uint32_t tempVal;

   struct FpgaPromData prom;
   struct FpgaProm_Reg *reg = (struct FpgaProm_Reg*)base;

   if ((ret = copy_from_user(&prom,(void *)arg,sizeof(struct FpgaPromData)))) {
      dev_warn(dev->device,"PromWrite: copy_from_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &prom);
      return(-1);
   }

   if ( dev->debug > 0 )
      dev_info(dev->device,"PromWrite: Addr=0x%x, Cmd=0x%x, Data=0x%x.\n", prom.address, prom.cmd, prom.data);

   // Set the data bus
   tempVal = ( (prom.cmd << 16) | prom.data );
   iowrite32(tempVal,&(reg->promData));

   asm("nop");

   // Set the address bus and initiate the transfer
   tempVal = (~0x80000000 & prom.address);
   iowrite32(tempVal,&(reg->promAddr));
   asm("nop");
   return(0);
}

// Prom write
int32_t FpgaProm_Read(struct DmaDevice *dev, void * base, uint64_t arg) {
   int32_t  ret;
   uint32_t tempVal;

   struct FpgaPromData prom;
   struct FpgaProm_Reg *reg = (struct FpgaProm_Reg*)base;

   if ((ret=copy_from_user(&prom,(void *)arg,sizeof(struct FpgaPromData)))) {
      dev_warn(dev->device,"PromRead: copy_from_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &prom);
      return(-1);
   }

   // Set the data bus
   tempVal = ( (prom.cmd << 16) | 0xFF );
   iowrite32(tempVal,&(reg->promData));
   asm("nop");

   // Set the address bus and initiate the transfer
   tempVal = (0x80000000 | prom.address);
   iowrite32(tempVal,&(reg->promAddr));
   asm("nop");

   // Read the data register
   prom.data = ioread32(&(reg->promRead));

   if ( dev->debug > 0 )
      dev_info(dev->device,"PromRead: Addr=0x%x, Cmd=0x%x, Data=0x%x.\n", prom.address, prom.cmd, prom.data);

   // Return the data structure
   if ((ret=copy_to_user((void *)arg,&prom,sizeof(struct FpgaPromData)))) {
      dev_warn(dev->device,"PromRead: copy_to_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &prom);
      return(-1);
   }
   return(0);
}

