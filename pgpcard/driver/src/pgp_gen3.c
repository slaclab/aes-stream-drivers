/**
 *-----------------------------------------------------------------------------
 * Title      : PGP Card Gen3 Functions
 * ----------------------------------------------------------------------------
 * File       : pgp_gen3.c
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-11
 * Last update: 2016-08-11
 * ----------------------------------------------------------------------------
 * Description:
 * Access functions for Gen32 PGP Cards
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
#include <PgpDriver.h>
#include <pgp_gen3.h>
#include <pgp_common.h>
#include <dma_buffer.h>
#include <linux/seq_file.h>
#include <linux/signal.h>
#include <linux/slab.h>

// Set functions for gen2 card
struct hardware_functions PgpCardG3_functions = {
   .irq          = PgpCardG3_Irq,
   .init         = PgpCardG3_Init,
   .clear        = PgpCardG3_Clear,
   .retRxBuffer  = PgpCardG3_RetRxBuffer,
   .sendBuffer   = PgpCardG3_SendBuffer,
   .command      = PgpCardG3_Command,
   .seqShow      = PgpCardG3_SeqShow
};

// Interrupt handler
irqreturn_t PgpCardG3_Irq(int irq, void *dev_id) {
   uint32_t    stat;
   uint32_t    descA;
   uint32_t    descB;
   uint32_t    dmaId;
   uint32_t    subId;
   irqreturn_t ret;

   struct DmaDesc      * desc;
   struct DmaBuffer    * buff;
   struct DmaDevice    * dev;
   struct PgpInfo      * info;
   struct PgpCardG3Reg * reg;

   dev  = (struct DmaDevice *)dev_id;
   reg  = (struct PgpCardG3Reg *)dev->reg;
   info = (struct PgpInfo *)dev->hwData;

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

                  // Adjust VC for interleaved version
                  // Each dma engine is a VC
                  if ( info->type == PGP_GEN3_VCI ) {
                     buff->dest  = (dmaId / 2) * 4; // lane 
                     buff->dest += (dmaId % 2);     // vc
                  } else {
                     buff->dest  = dmaId * 4;
                     buff->dest += subId;
                  }

                  // Setup errors
                  if ( (descA >> 31) & 0x1) buff->error |= DMA_ERR_FIFO;
                  if ( (descA >> 30) & 0x1) buff->error |= PGP_ERR_EOFE;

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
void PgpCardG3_Init(struct DmaDevice *dev) {
   uint32_t maxFrame;
   uint32_t tmp;
   uint64_t tmpL;
   uint32_t x;

   struct PgpInfo      * info;
   struct PgpCardG3Reg * reg;
   reg = (struct PgpCardG3Reg *)dev->reg;

   // Remove card reset, bit 1 of control register
   tmp = ioread32(&(reg->cardRstStat));
   tmp &= 0xFFFFFFFD;
   iowrite32(tmp,&(reg->cardRstStat));

   // Setup max frame value
   maxFrame = dev->cfgSize / 4;
   maxFrame |= 0x80000000;

   // Continue enabled
   if ( dev->cfgCont ) maxFrame |= 0x40000000;
   dev_info(dev->device,"Init: Setting rx continue flag=%i.\n", dev->cfgCont);

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
   dev->hwData = (void *)kmalloc(sizeof(struct PgpInfo),GFP_KERNEL);
   info = (struct PgpInfo *)dev->hwData;

   memset(info,0,sizeof(struct PgpInfo));

   info->version = ioread32(&(reg->version));

   // Form serial number
   tmpL = ioread32(&(reg->serNumUpper));
   info->serial = tmpL << 32;
   tmpL = ioread32(&(reg->serNumLower));
   info->serial |= tmpL;

   for (x=0; x < 64; x++) {
      ((uint32_t *)info->buildStamp)[x] = ioread32((&reg->BuildStamp[x]));
   }          
   info->pgpRate = ioread32(&(reg->pgpRate));
   memset(dev->destMask,0,DMA_MASK_SIZE);

   // Card info
   if ( (ioread32(&(reg->vciMode)) & 0x1) != 0 ) {
      info->type = PGP_GEN3_VCI;
      info->laneMask   = 0x0F;
      info->vcPerMask  = 0x3;
      dev->destMask[0] = 0x33;
      dev->destMask[1] = 0x33;
   } else {
      info->type = PGP_GEN3;
      info->laneMask   = 0xFF;
      info->vcPerMask  = 0xF;
      dev->destMask[0] = 0xFF;
      dev->destMask[1] = 0xFF;
      dev->destMask[2] = 0xFF;
      dev->destMask[3] = 0xFF;
   }
   info->promPrgEn  = 1;
   info->evrSupport = 1;

   // Enable interrupts
   iowrite32(1,&(reg->irq));

   dev_info(dev->device,"Init: Found card. Version=0x%x, Type=0x%.2x\n", 
         info->version,info->type);
}


// Clear card in top level Remove
void PgpCardG3_Clear(struct DmaDevice *dev) {
   uint32_t tmp;
   struct PgpCardG3Reg *reg;
   reg = (struct PgpCardG3Reg *)dev->reg;

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
void PgpCardG3_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer *buff) {
   struct PgpCardG3Reg *reg;
   reg = (struct PgpCardG3Reg *)dev->reg;

   if ( dmaBufferToHw(buff) < 0 ) 
      dev_warn(dev->device,"RetRxBuffer: Failed to map dma buffer.\n");
   else iowrite32(buff->buffHandle,&(reg->rxFree[buff->owner]));
}


// Send a buffer
int32_t PgpCardG3_SendBuffer(struct DmaDevice *dev, struct DmaBuffer *buff) {
   uint32_t descA;
   uint32_t descB;
   uint32_t dmaId;
   uint32_t subId;

   struct PgpInfo      * info;
   struct PgpCardG3Reg * reg;

   reg  = (struct PgpCardG3Reg *)dev->reg;
   info = (struct PgpInfo *)dev->hwData;

   // Lane remap for VC interleaved card where each DMA engine is a single VC
   if ( info->type == PGP_GEN3_VCI ) {
      dmaId = ((buff->dest / 4) * 2) + (buff->dest % 4);
      subId = 0;
   } else {
      dmaId = buff->dest / 4;
      subId = buff->dest % 4;
   }

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
int32_t PgpCardG3_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   uint32_t mask;
   uint32_t tempLane;
   uint32_t tempVal;
   uint32_t tmp;
   int32_t  ret;

   struct PgpInfo *      info;
   struct PgpStatus      status;
   struct PciStatus      pciStatus;
   struct PgpCardG3Reg * reg;
   struct pgpprom_reg  * preg;
   struct PgpEvrControl  evrControl;
   struct PgpEvrStatus   evrStatus;

   reg  = (struct PgpCardG3Reg *)dev->reg;
   info = (struct PgpInfo * )dev->hwData;

   switch (cmd) {

      // Control loopback
      case PGP_Set_Loop:
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

      // Reset counters
      case PGP_Count_Reset:
         spin_lock(&dev->commandLock);
         tmp = ioread32(&(reg->pgpCardStat[0])); // Store old reg val
         iowrite32(tmp|0x1,&(reg->pgpCardStat[0])); // Set reset bit
         iowrite32(tmp,&(reg->pgpCardStat[0])); // Set old reg val
         spin_unlock(&dev->commandLock);
         if (dev->debug > 0) dev_info(dev->device,"Count reset\n");
         return(0);
         break;

      // Send OpCode
      case PGP_Send_OpCode:
         iowrite32(arg&0xFF,&(reg->pgpOpCode));
         if (dev->debug > 0) dev_info(dev->device,"Send OP-Code: %x\n", (uint8_t)arg);
         return(0);
         break;

      // Set lane sideband data
      case PGP_Set_Data:
         tempLane = arg & 0xF;
         tempVal  = (arg >> 8) & 0xFF;

         if ( tempLane > 8 ) return(0);

         iowrite32(tempVal,&(reg->pgpData[tempLane]));

         // Debug
         if (dev->debug > 0) dev_info(dev->device,"Set local data for %i to %i\n", tempLane, tempVal);
         return(-1);
         break;

      // Read card info
      case PGP_Read_Info:
         if ((ret=copy_to_user((void *)arg,info,sizeof(struct PgpInfo)))) {
            dev_warn(dev->device,"Command: copy_to_user failed. ret=%i, user=%p kern=%p\n",
                ret, (void *)arg, info);
            return(-1);
         }
         return(0);
         break;

      // Read PCI Status
      case PGP_Read_Pci:
         PgpCardG3_GetPci(dev,&pciStatus);

         if ((ret=copy_to_user((void *)arg,&pciStatus,sizeof(struct PciStatus)))) {
            dev_warn(dev->device,"Command: copy_to_user failed. ret=%i, user=%p kern=%p\n",
                ret, (void *)arg, &pciStatus);
            return(-1);
         }
         return(0);
         break;

      // Read status for a lane
      case PGP_Read_Status:
         if ((ret=copy_from_user(&status,(void *)arg,sizeof(struct PgpStatus)))) {
            dev_warn(dev->device,"Command: copy_from_user failed. ret=%i, user=%p kern=%p\n",
                ret, (void *)arg, &status);
            return(-1);
         }

         PgpCardG3_GetStatus(dev,&status,status.lane);

         if ((ret=copy_to_user((void *)arg,&status,sizeof(struct PgpStatus)))) {
            dev_warn(dev->device,"Command: copy_to_user failed. ret=%i, user=%p kern=%p\n",
                ret, (void *)arg, &status);
            return(-1);
         }
         return(0);
         break;

      // Set EVR lane control
      case PGP_Set_Evr_Cntrl:
         if ((ret=copy_from_user(&evrControl,(void *)arg,sizeof(struct PgpEvrControl)))) {
            dev_warn(dev->device,"Command: copy_from_user failed. ret=%i, user=%p kern=%p\n",
                ret, (void *)arg, &evrControl);
            return(-1);
         }

         PgpCardG3_SetEvrControl(dev, &evrControl, evrControl.lane);
         return(0);

      // Get EVR lane control
      case PGP_Get_Evr_Cntrl:
         if ((ret=copy_from_user(&evrControl,(void *)arg,sizeof(struct PgpEvrControl)))) {
            dev_warn(dev->device,"Command: copy_from_user failed. ret=%i, user=%p kern=%p\n",
                ret, (void *)arg, &evrControl);
            return(-1);
         }

         PgpCardG3_GetEvrControl(dev, &evrControl, evrControl.lane);

         if ((ret=copy_to_user((void *)arg,&evrControl,sizeof(struct PgpEvrControl)))) {
            dev_warn(dev->device,"Command: copy_to_user failed. ret=%i, user=%p kern=%p\n",
                ret, (void *)arg, &evrControl);
            return(-1);
         }
         return(0);
         break;

      // Read EVR lane status
      case PGP_Get_Evr_Status:
         if ((ret=copy_from_user(&evrStatus,(void *)arg,sizeof(struct PgpEvrStatus)))) {
            dev_warn(dev->device,"Command: copy_from_user failed. ret=%i, user=%p kern=%p\n",
                ret, (void *)arg, &evrStatus);
            return(-1);
         }

         PgpCardG3_GetEvrStatus(dev, &evrStatus, evrStatus.lane);

         if ((ret=copy_to_user((void *)arg,&evrStatus,sizeof(struct PgpEvrStatus)))) {
            dev_warn(dev->device,"Command: copy_to_user failed. ret=%i, user=%p kern=%p\n",
                ret, (void *)arg, &evrStatus);
            return(-1);
         }
         return(0);
         break;
      
      // Reset EVR counters   
      case PGP_Rst_Evr_Count:
         tempLane = arg & 0x07;
         spin_lock(&dev->commandLock);
         tempVal = ioread32(&(reg->evrCardStat[0])); // Store old reg val
         iowrite32(tempVal|(0x1<<(tempLane+8)),&(reg->evrCardStat[0])); // Set reset bit
         iowrite32(tempVal,&(reg->evrCardStat[0])); // Set old reg val
         spin_unlock(&dev->commandLock);
         return(0);
         break;

      // Write to prom
      case PGP_Write_Prom:
         preg = (struct pgpprom_reg *)&(reg->promData);
         return(PgpCard_PromWrite(dev,preg,arg));
         break;

      // Read from prom
      case PGP_Read_Prom:
         preg = (struct pgpprom_reg *)&(reg->promData);
         return(PgpCard_PromRead(dev,preg,arg));
         break;

      default:
         dev_warn(dev->device,"Command: Invalid command=%i\n",cmd);
         return(-1);
         break;
   }
   return(-1);
}


// Add data to proc dump
void PgpCardG3_SeqShow(struct seq_file *s, struct DmaDevice *dev) {
   uint32_t tmp;
   uint32_t x;

   struct PgpInfo *      info;
   struct PciStatus      pci;
   struct PgpStatus      status;
   struct PgpCardG3Reg * reg;
   struct PgpEvrStatus   evrStatus;
   struct PgpEvrControl  evrControl;

   reg  = (struct PgpCardG3Reg *)dev->reg;
   info = (struct PgpInfo * )dev->hwData;

   seq_printf(s,"\n");
   PgpCard_InfoShow(s,info);
   seq_printf(s,"\n");
   PgpCardG3_GetPci(dev,&pci);
   PgpCard_PciShow(s,&pci);

   for (x=0; x < 8; x++) {
      if ( ((1 << x) & info->laneMask) == 0 ) continue;
      PgpCardG3_GetStatus(dev,&status,x);
      seq_printf(s,"\n");
      PgpCard_LaneShow(s,&status);
   }

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
  
   for (x=0; x < 8; x++) {
      PgpCardG3_GetEvrStatus(dev,&evrStatus,x);
      PgpCardG3_GetEvrControl(dev,&evrControl,x);
      seq_printf(s,"\n");
      seq_printf(s,"-------------- EVR Lane %i -----------------\n",x);
      seq_printf(s,"            evrEnable : %i\n",evrControl.evrEnable);
      seq_printf(s,"          laneRunMask : %i\n",evrControl.laneRunMask);
      seq_printf(s,"          startStopEn : %i\n",evrControl.evrSyncEn);
      seq_printf(s,"           modeSelect : %i\n",evrControl.evrSyncSel);
      seq_printf(s,"           headerMask : %i\n",evrControl.headerMask);
      seq_printf(s,"        startStopWord : %i\n",evrControl.evrSyncWord);
      seq_printf(s,"              runCode : %i\n",evrControl.runCode);
      seq_printf(s,"           acceptCode : %i\n",evrControl.acceptCode);
      seq_printf(s,"             runDelay : %i\n",evrControl.runDelay);
      seq_printf(s,"          acceptDelay : %i\n",evrControl.acceptDelay);
      seq_printf(s,"           linkErrors : %i\n",evrStatus.linkErrors);
      seq_printf(s,"               linkUp : %i\n",evrStatus.linkUp);
      seq_printf(s,"            runStatus : %i\n",evrStatus.runStatus);
      seq_printf(s,"           evrSeconds : %i\n",evrStatus.evrSeconds);
      seq_printf(s,"           runCounter : %i\n",evrStatus.runCounter);
      seq_printf(s,"        acceptCounter : %i\n",evrStatus.runCounter);
   }             
}


// Get PCI Status
void PgpCardG3_GetPci(struct DmaDevice *dev, struct PciStatus *status) {
   uint32_t tmp;

   struct PgpCardG3Reg *reg;
   reg = (struct PgpCardG3Reg *)dev->reg;

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


// Get Lane Status
void PgpCardG3_GetStatus(struct DmaDevice *dev, struct PgpStatus *status, uint8_t lane) {
   uint32_t tempVal;

   struct PgpCardG3Reg *reg;
   reg = (struct PgpCardG3Reg *)dev->reg;

   lane &= 0x7;

   memset(status,0,sizeof(struct PgpStatus));
   status->lane = lane;

   tempVal = ioread32(&(reg->pgpCardStat[0]));

   if ( lane < 2 ) {
      status->txReady  = ((tempVal >> (lane+30)) & 0x1);
      status->rxReady  = ((tempVal >> (lane+28)) & 0x1);
   }
   status->loopBack = ((tempVal >> (lane+0))  & 0x1);

   tempVal = ioread32(&(reg->pgpCardStat[1]));
   status->remLinkReady = (tempVal >> (lane+8))  & 0x1;
   status->locLinkReady = (tempVal >> (lane+0))  & 0x1;

   tempVal = ioread32(&(reg->pgpLaneStat[lane]));
   status->linkErrCnt  = (tempVal >> 28) & 0xF;
   status->linkDownCnt = (tempVal >> 24) & 0xF;
   status->cellErrCnt  = (tempVal >> 20) & 0xF;
   status->fifoErr     = (((tempVal >> 16) & 0xF) != 0);
   status->rxCount    += ((tempVal>>12) & 0xF);
   status->rxCount    += ((tempVal>> 8) & 0xF);
   status->rxCount    += ((tempVal>> 4) & 0xF);
   status->rxCount    += ((tempVal>> 0) & 0xF);

   status->remData = (ioread32(&(reg->pgpData[lane])) >> 8) & 0xFF;
   //status->remBuffStatus =

}


// Get EVR Status
void PgpCardG3_GetEvrStatus(struct DmaDevice *dev, struct PgpEvrStatus *status, uint8_t lane) {
   uint32_t tempVal;

   struct PgpCardG3Reg *reg;
   reg = (struct PgpCardG3Reg *)dev->reg;

   lane &= 0x7;

   memset(status,0,sizeof(struct PgpEvrStatus));
   status->lane = lane;

   tempVal = ioread32(&(reg->evrCardStat[0]));
   status->linkUp     = (tempVal >>  4) & 0x1;

   tempVal = ioread32(&(reg->evrCardStat[1]));
   status->runStatus  = (tempVal >> (24+lane)) & 0x1;

   tempVal = ioread32(&(reg->evrCardStat[3]));
   status->linkErrors = tempVal;

   tempVal = ioread32(&(reg->evrCardStat[4]));
   status->evrSeconds = tempVal;

   tempVal = ioread32(&(reg->evrRunCnt[lane]));
   status->runCounter = tempVal;

   tempVal = ioread32(&(reg->acceptCnt[lane]));
   status->acceptCounter = tempVal;
}


// Get EVR Config
void PgpCardG3_GetEvrControl(struct DmaDevice *dev, struct PgpEvrControl *control, uint8_t lane) {
   uint32_t tempVal;

   struct PgpCardG3Reg *reg;
   reg = (struct PgpCardG3Reg *)dev->reg;

   lane &= 0x7;

   memset(control,0,sizeof(struct PgpEvrControl));
   control->lane = lane;

   tempVal = ioread32(&(reg->syncCode[lane]));
   control->evrSyncWord = tempVal;

   tempVal = ioread32(&(reg->runCode[lane]));
   control->runCode = tempVal;

   tempVal = ioread32(&(reg->acceptCode[lane]));
   control->acceptCode = tempVal;

   tempVal = ioread32(&(reg->runDelay[lane]));
   control->runDelay = tempVal;

   tempVal = ioread32(&(reg->acceptDelay[lane]));
   control->acceptDelay = tempVal;

   tempVal = ioread32(&(reg->evrCardStat[2]));
   control->headerMask = ((tempVal >> (lane*4)) & 0xF);

   tempVal = ioread32(&(reg->evrCardStat[1]));
   control->evrEnable = tempVal & 0x1;
   control->evrSyncEn = ((tempVal >> (16+lane)) & 0x1);
   control->evrSyncSel = ((tempVal >> (8+lane)) & 0x1);

   tempVal = ioread32(&(reg->evrCardStat[0]));
   control->laneRunMask = ((tempVal >> (16+lane)) & 0x1);
}


// Set EVR Config
void PgpCardG3_SetEvrControl(struct DmaDevice *dev, struct PgpEvrControl *control, uint8_t lane) {
   uint32_t tempVal;
   uint32_t mask;

   struct PgpCardG3Reg *reg;
   reg = (struct PgpCardG3Reg *)dev->reg;

   lane &= 0x7;

   spin_lock(&dev->commandLock);

   iowrite32(control->evrSyncWord,&(reg->syncCode[lane]));

   iowrite32(control->runCode,&(reg->runCode[lane]));

   iowrite32(control->acceptCode,&(reg->acceptCode[lane]));

   iowrite32(control->runDelay,&(reg->runDelay[lane]));

   iowrite32(control->acceptDelay,&(reg->acceptDelay[lane]));

   tempVal = ioread32(&(reg->evrCardStat[2]));
   mask = 0xFFFFFFFF ^ (0xF << (lane*4));
   tempVal &= mask;
   tempVal |= (control->headerMask << (lane*4));
   iowrite32(tempVal,&(reg->evrCardStat[2]));

   tempVal = ioread32(&(reg->evrCardStat[1]));
   mask = 0xFFFFFFFE;
   tempVal &= mask;
   tempVal |= control->evrEnable;

   mask = 0xFFFFFFFF ^ (0x1 << (lane+16));
   tempVal &= mask;
   tempVal |= (control->evrSyncEn << (lane+16));

   mask = 0xFFFFFFFF ^ (0x1 << (lane+8));
   tempVal &= mask;
   tempVal |= (control->evrSyncSel << (lane+8));

   iowrite32(tempVal,&(reg->evrCardStat[1]));

   tempVal = ioread32(&(reg->evrCardStat[0]));
   mask = 0xFFFFFFFF ^ (0x1 << (lane+16));
   tempVal &= mask;
   tempVal |= (control->laneRunMask << (lane+8));
   iowrite32(tempVal,&(reg->evrCardStat[0]));

   spin_unlock(&dev->commandLock);
}

