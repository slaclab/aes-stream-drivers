/**
 *-----------------------------------------------------------------------------
 * Title      : TEM Card Gen3 Functions
 * ----------------------------------------------------------------------------
 * File       : tem_gen3.c
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-11
 * Last update: 2016-08-11
 * ----------------------------------------------------------------------------
 * Description:
 * Access functions for TEM. Based upon the PGP generation 3 card.
 * ----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to 
 * the license terms in the LICENSE.txt file found in the top-level directory 
 * of this distribution and at: 
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
 * No part of the aes_stream_drivers_package, including this file, may be 
 * copied, modified, propagated, or distributed except according to the terms 
 * contained in the LICENSE.txt file.
 * ----------------------------------------------------------------------------
**/
#include <tem_gen3.h>
#include <fpga_prom.h>
#include <dma_buffer.h>
#include <linux/seq_file.h>
#include <linux/signal.h>
#include <linux/slab.h>

// Set functions for gen2 card
struct hardware_functions TemG3_functions = {
   .irq          = TemG3_Irq,
   .init         = TemG3_Init,
   .enable       = TemG3_Enable,
   .clear        = TemG3_Clear,
   .retRxBuffer  = TemG3_RetRxBuffer,
   .sendBuffer   = TemG3_SendBuffer,
   .command      = TemG3_Command,
   .seqShow      = TemG3_SeqShow
};

// Interrupt handler
irqreturn_t TemG3_Irq(int irq, void *dev_id) {
   uint32_t    stat;
   uint32_t    descA;
   uint32_t    descB;
   uint32_t    dmaId;
   uint32_t    subId;
   irqreturn_t ret;

   struct DmaDesc      * desc;
   struct DmaBuffer    * buff;
   struct DmaDevice    * dev;
   struct TemG3Reg * reg;

   dev  = (struct DmaDevice *)dev_id;
   reg  = (struct TemG3Reg *)dev->reg;

   // Read IRQ Status
   stat = ioread32(&(reg->irq));
   asm("nop");

   // Is this the source
   if ( (stat & 0x2) != 0 ) {

      if ( dev->debug > 0 ) dev_info(dev->device,"Irq: IRQ Called.\n");

      // Disable interrupts
      iowrite32(0,&(reg->irq));

      // Read Tx completion status
      stat = ioread32(&(reg->txStat[1]));
      asm("nop");

      // Tx Data is ready
      if ( (stat & 0x80000000) != 0 ) {

         do {

            // Read dma value
            stat = ioread32(&(reg->txRead));
            asm("nop");

            if ( (stat & 0x1) == 0x1 ) {

               if ( dev->debug > 0 ) 
                  dev_info(dev->device,"Irq: Return TX Status Value %.8x.\n",stat);

               // Attempt to find buffer in tx pool and return. otherwise return rx entry to hw.
               if ((buff = dmaRetBufferIrq (dev,stat&0xFFFFFFFC)) != NULL) {
                  iowrite32((stat & 0xFFFFFFFC), &(reg->rxFree[buff->owner]));
               }
            }

         // Repeat while next valid flag is set
         } while ( (stat & 0x1) == 0x1 );
      }

      // Read Rx completion status
      stat = ioread32(&(reg->rxStatus));
      asm("nop");

      // Data is ready
      if ( (stat & 0x80000000) != 0 ) {
         do {

            // Read descriptor
            descA = ioread32(&(reg->rxRead[0]));
            asm("nop");
            descB = ioread32(&(reg->rxRead[1]));
            asm("nop");

            if ( ( descB & 0x1) == 0x1 ) {

               // Find RX buffer entry
               if ((buff = dmaFindBufferList (&(dev->rxBuffers),descB&0xFFFFFFFC)) != NULL) {

                  // Extract data from descriptor
                  buff->count++;
                  buff->flags = (descA >> 29) & 0x1; // Bit  29 (CONT)
                  dmaId       = (descA >> 26) & 0x7; // Bits 28:26
                  subId       = (descA >> 24) & 0x3; // Bits 25:24
                  buff->size  = (descA & 0x00FFFFFF) * 4;  // 23:00
                  buff->error = 0;

                  // Only two dma engines are used
                  buff->dest = dmaId;

                  // Setup errors
                  if ( (descA >> 31) & 0x1) buff->error |= DMA_ERR_FIFO;
                  if ( (descA >> 30) & 0x1) buff->error |= TEM_ERR_EOFE;

                  // Bit 1 of descB is the or of all errors, determine len error if others are not set
                  if (( (descB >>  1) & 0x1) && (buff->error == 0) ) buff->error |= DMA_ERR_LEN;

                  if ( dev->debug > 0 ) {
                     dev_info(dev->device,"Irq: Rx Bytes=%i, Dest=%x, Error=0x%x, Cont=%i.\n",
                        buff->size, buff->dest, buff->error, buff->flags);
                  }

                  // Lock mask records
                  // This ensures close does not occur while irq routine is 
                  // pushing data to desc rx queue
                  spin_lock(&dev->maskLock);

                  // Find owner of lane/vc
                  desc = dev->desc[buff->dest];

                  // Return entry to FPGA if lane/vc is not open
                  if ( desc == NULL ) {
                     if ( dev->debug > 0 ) {
                        dev_info(dev->device,"Irq: Port not open return to free list.\n");
                     }
                     iowrite32((descB & 0xFFFFFFFC), &(reg->rxFree[(descA >> 26) & 0x7]));
                  }

                  // lane/vc is open, Add to RX Queue
                  else dmaRxBuffer(desc,buff);

                  // Unlock
                  spin_unlock(&dev->maskLock);
               } 

               // Buffer was not found
               else dev_warn(dev->device,"Irq: Failed to locate RX descriptor %.8x.\n",
                     (uint32_t)(descB&0xFFFFFFFC));
           }

         // Repeat while next valid flag is set
         } while ( (descB & 0x1) == 0x1 );
      }

      // Enable interrupts
      if ( dev->debug > 0 ) 
         dev_info(dev->device,"Irq: Done.\n");
      iowrite32(1,&(reg->irq));
      ret = IRQ_HANDLED;
   }
   else ret = IRQ_NONE;
   return(ret);
}

// Init card in top level Probe
void TemG3_Init(struct DmaDevice *dev) {
   uint32_t maxFrame;
   uint32_t tmp;
   uint64_t tmpL;
   uint32_t x;

   struct TemInfo  * info;
   struct TemG3Reg * reg;
   reg = (struct TemG3Reg *)dev->reg;

   // Remove card reset, bit 1 of control register
   tmp = ioread32(&(reg->cardRstStat));
   tmp &= 0xFFFFFFFD;
   iowrite32(tmp,&(reg->cardRstStat));

   // Setup max frame value
   maxFrame = dev->cfgSize;
   maxFrame |= 0x80000000;

   // Set to hardware 
   iowrite32(maxFrame,&(reg->rxMaxFrame));

   // Push receive buffers to hardware
   // Distribute rx bufferes evently between free lists
   for (x=0; x < dev->rxBuffers.count; x++) {
      if ( dmaBufferToHw(dev->rxBuffers.indexed[x]) < 0 ) 
         dev_warn(dev->device,"Init: Failed to map dma buffer.\n");
      else {
         iowrite32(dev->rxBuffers.indexed[x]->buffHandle,&(reg->rxFree[x % 8]));
         dev->rxBuffers.indexed[x]->owner = (x % 8);
      }
   }

   // Init hardware info
   dev->hwData = (void *)kmalloc(sizeof(struct TemInfo),GFP_KERNEL);
   info = (struct TemInfo *)dev->hwData;

   memset(info,0,sizeof(struct TemInfo));

   info->version = ioread32(&(reg->version));

   // Form serial number
   tmpL = ioread32(&(reg->serNumUpper));
   info->serial = tmpL << 32;
   tmpL = ioread32(&(reg->serNumLower));
   info->serial |= tmpL;

   for (x=0; x < 64; x++) {
      ((uint32_t *)info->buildStamp)[x] = ioread32((&reg->BuildStamp[x]));
   }          
   memset(dev->destMask,0,DMA_MASK_SIZE);
   dev->destMask[0] = 0x3;
   info->promPrgEn = 1;

   dev_info(dev->device,"Init: Found card. Version=0x%x\n",info->version);
}

// Enable the card
void TemG3_Enable(struct DmaDevice *dev) {
   struct TemG3Reg * reg;
   reg = (struct TemG3Reg *)dev->reg;

   // Enable interrupts
   iowrite32(1,&(reg->irq));
}

// Clear card in top level Remove
void TemG3_Clear(struct DmaDevice *dev) {
   uint32_t tmp;
   struct TemG3Reg *reg;
   reg = (struct TemG3Reg *)dev->reg;

   // Disable interrupts
   iowrite32(0,&(reg->irq));

   // Clear RX buffer
   iowrite32(0,&(reg->rxMaxFrame));

   // Set card reset, bit 1 of control register
   tmp = ioread32(&(reg->cardRstStat));
   tmp |= 0x00000002;
   iowrite32(tmp,&(reg->cardRstStat));

   // Clear hw data
   kfree(dev->hwData);
}


// Return receive buffer to card
// Single write so we don't need to lock
void TemG3_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer *buff) {
   struct TemG3Reg *reg;
   reg = (struct TemG3Reg *)dev->reg;

   if ( dmaBufferToHw(buff) < 0 )
      dev_warn(dev->device,"RetRxBuffer: Failed to map dma buffer.\n");
   else iowrite32(buff->buffHandle,&(reg->rxFree[buff->owner]));
}


// Send a buffer
int32_t TemG3_SendBuffer(struct DmaDevice *dev, struct DmaBuffer *buff) {
   uint32_t descA;
   uint32_t descB;
   uint32_t dmaId;
   uint32_t subId;

   struct TemG3Reg * reg;

   reg  = (struct TemG3Reg *)dev->reg;

   // each DMA lane is a destination
   dmaId = buff->dest;
   subId = 0;

   if ( dmaBufferToHw(buff) < 0 ) {
      dev_warn(dev->device,"SendBuffer: Failed to map dma buffer.\n");
      return(-1);
   }

   // Generate Tx descriptor
   descA  = (buff->flags << 26) & 0x04000000; // Bits 26    = Cont
   descA += (subId       << 24) & 0x03000000; // Bits 25:24 = VC
   descA += (buff->size  / 4  ) & 0x00FFFFFF; // Bits 23:0  = Length
   descB = buff->buffHandle;

   // Lock hw
   spin_lock(&dev->writeHwLock);

   // Write descriptor
   iowrite32(descA,&(reg->txWrA[dmaId]));
   asm("nop");
   iowrite32(descB,&(reg->txWrB[dmaId]));
   asm("nop");

   // UnLock hw
   spin_unlock(&dev->writeHwLock);

   return(buff->size);
}


// Execute command
int32_t TemG3_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   uint32_t mask;
   uint32_t tempLane;
   uint32_t tempVal;
   uint32_t tmp;
   int32_t  ret;

   struct TemInfo *  info;
   struct TemG3Reg * reg;
   struct PciStatus  pciStatus;

   reg  = (struct TemG3Reg *)dev->reg;
   info = (struct TemInfo * )dev->hwData;

   switch (cmd) {

      // Control loopback
      case TEM_Set_Loop:
         tempLane = arg & 0xFF;
         tempVal  = (arg >> 8) & 0x1;

         if ( tempLane > 8 ) return(0);

         spin_lock(&dev->commandLock);

         // Set loop
         if ( tempVal ) {
            tmp = ioread32(&(reg->pgpCardStat[0]));
            tmp |= (0x1 << ((tempLane&0x7) + 0));
            iowrite32(tmp,&(reg->pgpCardStat[0]));
            if (dev->debug > 0) dev_info(dev->device,"Set loopback for %u\n", tempLane);

         // Clear loop
         } else {
            mask = 0xFFFFFFFF ^ (0x1 << ((tempLane&0x7) + 0));  
            tmp = ioread32(&(reg->pgpCardStat[0]));
            tmp &= mask;
            iowrite32(tmp,&(reg->pgpCardStat[0]));
            if (dev->debug > 0) dev_info(dev->device,"Clr loopback for %u\n", tempLane);
         }
         spin_unlock(&dev->commandLock);
         return(0);
         break;

      // Read card info
      case TEM_Read_Info:
         if ((ret=copy_to_user((void *)arg,info,sizeof(struct TemInfo)))) {
            dev_warn(dev->device,"Command: copy_to_user failed. ret=%i, user=%p kern=%p\n",
                ret, (void *)arg, info);
            return(-1);
         }
         return(0);
         break;

      // Read PCI Status
      case TEM_Read_Pci:
         TemG3_GetPci(dev,&pciStatus);

         if ((ret=copy_to_user((void *)arg,&pciStatus,sizeof(struct PciStatus)))) {
            dev_warn(dev->device,"Command: copy_to_user failed. ret=%i, user=%p kern=%p\n",
                ret, (void *)arg, &pciStatus);
            return(-1);
         }
         return(0);
         break;

      // Write to prom
      case FPGA_Write_Prom:
         return(FpgaProm_Write(dev,reg->promRegs,arg));
         break;

      // Read from prom
      case FPGA_Read_Prom:
         return(FpgaProm_Read(dev,reg->promRegs,arg));
         break;

      default:
         dev_warn(dev->device,"Command: Invalid command=%i\n",cmd);
         return(-1);
         break;
   }
   return(-1);
}


// Add data to proc dump
void TemG3_SeqShow(struct seq_file *s, struct DmaDevice *dev) {
   uint32_t tmp;

   struct TemInfo *  info;
   struct TemG3Reg * reg;
   struct PciStatus  status;

   reg  = (struct TemG3Reg *)dev->reg;
   info = (struct TemInfo * )dev->hwData;

   seq_printf(s,"\n");
   seq_printf(s,"-------------- Card Info ------------------\n");
   seq_printf(s,"              Version : 0x%.8x\n",info->version);
   seq_printf(s,"               Serial : 0x%.16llx\n",info->serial);
   seq_printf(s,"           BuildStamp : %s\n",info->buildStamp);
   seq_printf(s,"            PromPrgEn : %i\n",info->promPrgEn);
   seq_printf(s,"\n");

   TemG3_GetPci(dev,&status);
   seq_printf(s,"-------------- PCI Info -------------------\n");
   seq_printf(s,"           PciCommand : 0x%.4x\n",status.pciCommand);
   seq_printf(s,"            PciStatus : 0x%.4x\n",status.pciStatus);
   seq_printf(s,"          PciDCommand : 0x%.4x\n",status.pciDCommand);
   seq_printf(s,"           PciDStatus : 0x%.4x\n",status.pciDStatus);
   seq_printf(s,"          PciLCommand : 0x%.4x\n",status.pciLCommand);
   seq_printf(s,"           PciLStatus : 0x%.4x\n",status.pciLStatus);
   seq_printf(s,"         PciLinkState : 0x%x\n",status.pciLinkState);
   seq_printf(s,"          PciFunction : 0x%x\n",status.pciFunction);
   seq_printf(s,"            PciDevice : 0x%x\n",status.pciDevice);
   seq_printf(s,"               PciBus : 0x%.2x\n",status.pciBus);
   seq_printf(s,"             PciLanes : %i\n",status.pciLanes);
   seq_printf(s,"\n");
   seq_printf(s,"-------------- General HW -----------------\n");

   seq_printf(s,"              TxCount : %i\n",ioread32(&(reg->txCount)));
   seq_printf(s,"              RxCount : %i\n",ioread32(&(reg->rxCount)));

   tmp = ioread32(&(reg->rxStatus));
   seq_printf(s,"          RxStatusRaw : 0x%.8x\n",tmp);
   seq_printf(s,"          RxReadReady : %i\n",(tmp >> 31)&0x1);
   seq_printf(s,"       RxRetFifoCount : %i\n",tmp&0x3FF);

   tmp = ioread32(&(reg->txStat[1]));
   seq_printf(s,"          TxReadReady : %i\n",(tmp >> 31)&0x1);
   seq_printf(s,"       TxRetFifoCount : %i\n",tmp&0x3FF);

   seq_printf(s,"           CountReset : %i\n",(ioread32(&(reg->cardRstStat)) >> 0) & 0x1);
   seq_printf(s,"            CardReset : %i\n",(ioread32(&(reg->cardRstStat)) >> 1) & 0x1);
}


// Get PCI Status
void TemG3_GetPci(struct DmaDevice *dev, struct PciStatus *status) {
   uint32_t tmp;

   struct TemG3Reg *reg;
   reg = (struct TemG3Reg *)dev->reg;

   memset(status,0,sizeof(struct PciStatus));

   tmp = ioread32(&(reg->pciStat[0]));
   status->pciCommand = ((tmp >> 16)&0xFFFF);
   status->pciStatus  = (tmp & 0xFFFF);

   tmp = ioread32(&(reg->pciStat[1]));
   status->pciDCommand = ((tmp >> 16)&0xFFFF);
   status->pciDStatus  = (tmp & 0xFFFF);

   tmp = ioread32(&(reg->pciStat[2]));
   status->pciLCommand = ((tmp >> 16)&0xFFFF);
   status->pciLStatus  = (tmp & 0xFFFF);
   status->pciLanes    = ((tmp >> 4) & 0x1F);

   tmp = ioread32(&(reg->pciStat[3]));
   status->pciLinkState = ((tmp >> 24)&0x7);
   status->pciFunction  = ((tmp >> 16)&0x3);
   status->pciDevice    = ((tmp >>  8)&0xF);
   status->pciBus       = (tmp&0xFF);
}


