/**
 *-----------------------------------------------------------------------------
 * Title      : PGP Card Gen1 & Gen2 Functions
 * ----------------------------------------------------------------------------
 * File       : pgp_gen2.c
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * Access functions for Gen1 & Gen2 PGP Cards
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
#include <FpgaProm.h>
#include <fpga_prom.h>
#include <pgp_common.h>
#include <pgp_gen2.h>
#include <linux/seq_file.h>
#include <linux/signal.h>
#include <linux/slab.h>

// Set functions for gen2 card
struct hardware_functions PgpCardG2_functions = {
   .irq          = PgpCardG2_Irq,
   .init         = PgpCardG2_Init,
   .enable       = PgpCardG2_Enable,
   .clear        = PgpCardG2_Clear,
   .retRxBuffer  = PgpCardG2_RetRxBuffer,
   .sendBuffer   = PgpCardG2_SendBuffer,
   .command      = PgpCardG2_Command,
   .seqShow      = PgpCardG2_SeqShow
};

// Interrupt handler
irqreturn_t PgpCardG2_Irq(int irq, void *dev_id) {
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
   struct PgpCardG2Reg * reg;

   dev  = (struct DmaDevice *)dev_id;
   reg  = (struct PgpCardG2Reg *)dev->reg;
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
      stat = ioread32(&(reg->txStatus));
      asm("nop");

      // Tx Data is ready
      if ( (stat & 0x00000400) != 0 ) {

         do {

            // Read dma value
            stat = ioread32(&(reg->txRead));
            asm("nop");
            if ( dev->debug > 0 ) 
               dev_info(dev->device,"Irq: Return TX Status Value %.8x.\n",stat);

            // Attempt to find buffer in tx pool and return. otherwise return rx entry to hw.
            if ((buff = dmaRetBufferIrq (dev,stat&0xFFFFFFFC)) != NULL) {
               iowrite32((stat & 0xFFFFFFFC),&(reg->rxFree));
            }

         // Repeat while next valid flag is set
         } while ( (stat & 0x2) != 0 );
      }

      // Read Rx completion status
      stat = ioread32(&(reg->rxStatus));
      asm("nop");

      // Data is ready
      if ( (stat & 0x00000400) != 0 ) {
         do {

            // Read descriptor
            descA = ioread32(&(reg->rxRead0));
            asm("nop");
            descB = ioread32(&(reg->rxRead1));
            asm("nop");

            // Find RX buffer entry
            if ((buff = dmaFindBufferList (&(dev->rxBuffers),descB&0xFFFFFFFC)) != NULL) {

               // Extract data from descriptor
               buff->count++;
               dmaId       = (descA >> 30) & 0x3;
               subId       = (descA >> 28) & 0x3;
               buff->flags = (descA >> 27) & 0x1;
               buff->size  = (descA & 0x00FFFFFF) * 4;
               buff->error = 0;

               // Set DEST, with adjustment for interleaved version
               // Each DMA engine is a VC
               if ( info->type == PGP_GEN2_VCI ) {
                  buff->dest  = (dmaId & 0x2) * 4; // Lane
                  buff->dest += (dmaId & 0x1);     // VC
               } 

               // Each lane has 4 VCs, one DMA engine per lane
               else {
                  buff->dest  = dmaId * 4; // Lane
                  buff->dest += subId;     // VC
               }

               // Setup errors
               if ( (descA >> 26) & 0x1) buff->error |= DMA_ERR_LEN;
               if ( (descA >> 25) & 0x1) buff->error |= DMA_ERR_FIFO;
               if ( (descA >> 24) & 0x1) buff->error |= PGP_ERR_EOFE;

               if ( dev->debug > 0 ) {
                  dev_info(dev->device,"Irq: Rx Size=%i, Dest=0x%x, Error=0x%x, Cont=%i.\n",
                     buff->size, buff->dest, buff->error, buff->flags);
               }

               // Lock mask records
               // This ensures close does not occur while irq routine is 
               // pushing data to desc rx queue
               spin_lock(&dev->maskLock);

               // Find owner of destination
               desc = dev->desc[buff->dest];

               // Return entry to FPGA if /vc is not open
               if ( desc == NULL ) {
                  if ( dev->debug > 0 ) {
                     dev_info(dev->device,"Irq: Port not open return to free list.\n");
                  }
                  iowrite32((descB & 0xFFFFFFFC),&(reg->rxFree));
               }

               // lane/vc is open,  Add to RX Queue
               else dmaRxBuffer(desc,buff);

               // Unlock
               spin_unlock(&dev->maskLock);
            }

            // Buffer was not found
            else dev_warn(dev->device,"Irq: Failed to locate RX descriptor 0x%.8x.\n",
                  (uint32_t)(descB&0xFFFFFFFC));

         // Repeat while next valid flag is set
         } while ( (descB & 0x2) != 0 );
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
void PgpCardG2_Init(struct DmaDevice *dev) {
   uint32_t tmp;
   uint32_t maxFrame;
   uint32_t x;

   struct PgpInfo      * info;
   struct PgpCardG2Reg * reg;

   reg = (struct PgpCardG2Reg *)dev->reg;

   // Remove card reset, bit 1 of control register
   tmp = ioread32(&(reg->control));
   tmp &= 0xFFFFFFFD;
   iowrite32(tmp,&(reg->control));

   // Setup max frame value
   maxFrame = dev->cfgSize / 4;
   maxFrame |= 0x80000000;

   // Continue enabled
   if ( dev->cfgCont ) maxFrame |= 0x40000000;
   dev_info(dev->device,"Init: Setting rx continue flag=%i.\n", dev->cfgCont);

   // Set to hardware 
   iowrite32(maxFrame,&(reg->rxMaxFrame));

   // Push receive buffers to hardware
   for (x=0; x < dev->rxBuffers.count; x++) {
      if ( dmaBufferToHw(dev->rxBuffers.indexed[x]) < 0 ) 
         dev_warn(dev->device,"Init: Failed to map dma buffer.\n");
      else iowrite32(dev->rxBuffers.indexed[x]->buffHandle,&(reg->rxFree));
   }

   // Init hardware info
   dev->hwData = (void *)kmalloc(sizeof(struct PgpInfo),GFP_KERNEL);
   info = (struct PgpInfo *)dev->hwData;

   memset(info,0,sizeof(struct PgpInfo));

   info->version    = ioread32(&(reg->version));
   info->pgpRate    = 3125;
   info->evrSupport = 0;
   memset(dev->destMask,0,DMA_MASK_SIZE);

   // Use firmware version to distinguish between gen1/gen2 & other special cards
   switch((info->version >> 12) & 0xFFFFF) {
      case 0xCEC80:
         dev->destMask[0] = 0xFF;
         dev->destMask[1] = 0xFF;
         info->type       = PGP_GEN1;
         info->laneMask   = 0xF;
         info->vcPerMask  = 0xF;
         info->promPrgEn  = 0;
         break;
      case 0xCEC82:
         dev->destMask[0] = 0xFF;
         dev->destMask[1] = 0xFF;
         info->type       = PGP_GEN2;
         info->laneMask   = 0xF;
         info->vcPerMask  = 0xF;
         info->promPrgEn  = 1;
         break;
      case 0xCEC83:
         dev->destMask[0] = 0x03;
         dev->destMask[1] = 0x03;
         info->type       = PGP_GEN2_VCI;
         info->laneMask   = 0x5;
         info->vcPerMask  = 0x3;
         info->promPrgEn  = 1;
         break;
      default:
         dev->destMask[0] = 0xFF;
         dev->destMask[1] = 0xFF;
         info->type       = PGP_GEN2;
         info->laneMask   = 0xF;
         info->vcPerMask  = 0xF;
         info->promPrgEn  = 1;
         break;
   }

   dev_info(dev->device,"Init: Found card. Version=0x%x, Type=0x%.2x\n", info->version,info->type);
}

// enable the card
void PgpCardG2_Enable(struct DmaDevice *dev) {
   struct PgpCardG2Reg * reg;

   reg = (struct PgpCardG2Reg *)dev->reg;

   // Enable interrupts
   iowrite32(1,&(reg->irq));
}

// Clear card in top level Remove
void PgpCardG2_Clear(struct DmaDevice *dev) {
   uint32_t tmp;
   struct PgpCardG2Reg *reg;
   reg = (struct PgpCardG2Reg *)dev->reg;

   // Disable interrupts
   iowrite32(0,&(reg->irq));

   // Clear RX buffer
   iowrite32(0,&(reg->rxMaxFrame));

   // Set card reset, bit 1 of control register
   tmp = ioread32(&(reg->control));
   tmp |= 0x00000002;
   iowrite32(tmp,&(reg->control));

   // Clear hw data
   kfree(dev->hwData);
}


// Return receive buffer to card
// Single write so we don't need to lock
void PgpCardG2_RetRxBuffer(struct DmaDevice *dev, struct DmaBuffer *buff) {
   struct PgpCardG2Reg *reg;
   reg = (struct PgpCardG2Reg *)dev->reg;

   if ( dmaBufferToHw(buff) < 0 ) 
      dev_warn(dev->device,"RetRxBuffer: Failed to map dma buffer.\n");
   else iowrite32(buff->buffHandle,&(reg->rxFree));
}


// Send a buffer
int32_t PgpCardG2_SendBuffer(struct DmaDevice *dev, struct DmaBuffer *buff) {
   uint32_t descA;
   uint32_t descB;
   uint32_t dmaId;
   uint32_t subId;

   struct PgpInfo      * info;
   struct PgpCardG2Reg * reg;

   if ( (buff->size % 4) != 0 ) {
      dev_warn(dev->device,"SendBuffer: Frame size not a multiple of 4.\n");
      dmaQueuePush(&(dev->tq),buff);
      return(-1);
   }

   reg  = (struct PgpCardG2Reg *)dev->reg;
   info = (struct PgpInfo *)dev->hwData;

   // Lane remap for VC interleaved card where each DMA engine is a single VC
   if ( info->type == PGP_GEN2_VCI ) {
      dmaId = (buff->dest / 4) + (buff->dest % 4);
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
   descA  = (dmaId       << 30) & 0xC0000000; // Bits 31:30 = Lane
   descA += (subId       << 28) & 0x30000000; // Bits 29:28 = VC
   descA += (buff->flags << 27) & 0x08000000; // Bits 27    = Cont
   descA += (buff->size / 4   ) & 0x00FFFFFF; // Bits 23:0  = Length
   descB = buff->buffHandle;

   // Lock hw
   spin_lock(&dev->writeHwLock);

   // Write descriptor
   switch ( dmaId ) {
      case 0:
         iowrite32(descA,&(reg->txL0Wr0));
         asm("nop");
         iowrite32(descB,&(reg->txL0Wr1));
         asm("nop");
         break;
      case 1:
         iowrite32(descA,&(reg->txL1Wr0));
         asm("nop");
         iowrite32(descB,&(reg->txL1Wr1));
         asm("nop");
         break;
      case 2:
         iowrite32(descA,&(reg->txL2Wr0));
         asm("nop");
         iowrite32(descB,&(reg->txL2Wr1));
         asm("nop");
         break;
      case 3:
         iowrite32(descA,&(reg->txL3Wr0));
         asm("nop");
         iowrite32(descB,&(reg->txL3Wr1));
         asm("nop");
         break;
      default: break;
   }

   // UnLock hw
   spin_unlock(&dev->writeHwLock);

   return(buff->size);
}


// Execute command
int32_t PgpCardG2_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   uint32_t mask;
   uint32_t tmp;
   uint32_t tempLane;
   uint32_t tempVal;
   int32_t  ret;

   struct PgpInfo *      info;
   struct PgpStatus      status;
   struct PciStatus      pciStatus;
   struct PgpCardG2Reg * reg;

   reg  = (struct PgpCardG2Reg *)dev->reg;
   info = (struct PgpInfo * )dev->hwData;

   switch (cmd) {

      // Control loopback
      case PGP_Set_Loop:
         tempLane = arg & 0xFF;
         tempVal  = (arg >> 8) & 0x1;

         if ( tempLane > 4 ) return(0);

         spin_lock(&dev->commandLock);

         // Set loop
         if ( tempVal ) {
            tmp = ioread32(&(reg->control));
            tmp |= ((0x10 << tempLane) & 0xF0);
            iowrite32(tmp,&(reg->control));
            if (dev->debug > 0) dev_info(dev->device,"Set loopback for %u\n", tempLane);

         // Clear loop
         } else {
            mask = 0xFFFFFFFF ^ ((0x10 << tempLane) & 0xF0);
            tmp = ioread32(&(reg->control));
            tmp &= mask;
            iowrite32(tmp,&(reg->control));
            if (dev->debug > 0) dev_info(dev->device,"Clr loopback for %u\n", tempLane);
         }
         spin_unlock(&dev->commandLock);
         return(0);
         break;

      // Reset counters
      case PGP_Count_Reset:
         spin_lock(&dev->commandLock);
         tmp = ioread32(&(reg->control)); // Store old reg val
         iowrite32(tmp|0x1,&(reg->control)); // Set reset bit
         iowrite32(tmp,&(reg->control)); // Set old reg val
         spin_unlock(&dev->commandLock);
         if (dev->debug > 0) dev_info(dev->device,"Count reset\n");
         return(0);
         break;

      // Set lane sideband data
      case PGP_Set_Data:
         tempLane = arg & 0xFF;
         tempVal  = (arg >> 8) & 0xFF;

         switch ( tempLane ) { 
            case 0x0: iowrite32(tempVal,&(reg->l0Data)); break;
            case 0x1: iowrite32(tempVal,&(reg->l1Data)); break;
            case 0x2: iowrite32(tempVal,&(reg->l2Data)); break;
            case 0x3: iowrite32(tempVal,&(reg->l3Data)); break;
            default: break;
         }

         // Debug
         if (dev->debug > 0) dev_info(dev->device,"Set local data for %u to %u\n", tempLane, tempVal);
         return(0);
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
         PgpCardG2_GetPci(dev,&pciStatus);

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

         PgpCardG2_GetStatus(dev,&status,status.lane);

         if ((ret=copy_to_user((void *)arg,&status,sizeof(struct PgpStatus)))) {
            dev_warn(dev->device,"Command: copy_to_user failed. ret=%i, user=%p kern=%p\n",
                ret, (void *)arg, &status);
            return(-1);
         }
         return(0);
         break;

      // Write to prom
      case FPGA_Write_Prom:
         if ( info->promPrgEn ) return(FpgaProm_Write(dev,reg->promRegs,arg));
         else return(-1);
         break;

      // Read from prom
      case FPGA_Read_Prom:
         if ( info->promPrgEn ) return(FpgaProm_Read(dev,reg->promRegs,arg));
         else return(-1);
         break;

      default:
         dev_warn(dev->device,"Command: Invalid command=%i.\n",cmd);
         return(-1);
         break;
   }
}

// Add data to proc dump
void PgpCardG2_SeqShow(struct seq_file *s, struct DmaDevice *dev) {
   uint32_t tmp;
   uint32_t cnt;
   uint32_t x;

   struct PgpInfo *      info;
   struct PciStatus      pci;
   struct PgpStatus      status;
   struct PgpCardG2Reg * reg;

   reg  = (struct PgpCardG2Reg *)dev->reg;
   info = (struct PgpInfo * )dev->hwData;

   seq_printf(s,"\n");
   PgpCard_InfoShow(s,info);
   seq_printf(s,"\n");
   PgpCardG2_GetPci(dev,&pci);
   PgpCard_PciShow(s,&pci);

   for (x=0; x < 4; x++) {
      if ( ((1 << x) & info->laneMask) == 0 ) continue;
      PgpCardG2_GetStatus(dev,&status,x);
      seq_printf(s,"\n");
      PgpCard_LaneShow(s,&status);
   }

   seq_printf(s,"\n");
   seq_printf(s,"-------------- General HW -----------------\n");

   seq_printf(s,"              TxCount : %i\n",ioread32(&(reg->txCount)));
   seq_printf(s,"              RxCount : %i\n",ioread32(&(reg->rxCount)));

   tmp = ioread32(&(reg->rxStatus));
   cnt = ((tmp >> 16)&0x3FF) + ((tmp >> 29)&0x1);

   seq_printf(s,"          RxFreeEmpty : %i\n",(tmp >> 31)&0x1);
   seq_printf(s,"          RxFreeFull  : %i\n",(tmp >> 30)&0x1);
   seq_printf(s,"          RxFreeValid : %i\n",(tmp >> 29)&0x1);
   seq_printf(s,"      RxFreeFifoCount : %i\n",(tmp >> 16)&0x3FF);
   seq_printf(s,"   Real Free Fifo Cnt : %i\n",cnt);
   seq_printf(s,"          RxReadReady : %i\n",(tmp >> 10)&0x1);
   seq_printf(s,"       RxRetFifoCount : %i\n",tmp&0x3FF);
}


// Get PCI Status
void PgpCardG2_GetPci(struct DmaDevice *dev, struct PciStatus *status) {
   uint32_t tmp;

   struct PgpCardG2Reg *reg;
   reg = (struct PgpCardG2Reg *)dev->reg;

   memset(status,0,sizeof(struct PciStatus));

   tmp = ioread32(&(reg->pciStat0));
   status->pciCommand = ((tmp >> 16)&0xFFFF);
   status->pciStatus  = (tmp & 0xFFFF);

   tmp = ioread32(&(reg->pciStat1));
   status->pciDCommand = ((tmp >> 16)&0xFFFF);
   status->pciDStatus  = (tmp & 0xFFFF);

   tmp = ioread32(&(reg->pciStat2));
   status->pciLCommand = ((tmp >> 16)&0xFFFF);
   status->pciLStatus  = (tmp & 0xFFFF);
   status->pciLanes    = ((tmp >> 4) & 0x1F);

   tmp = ioread32(&(reg->pciStat3));
   status->pciLinkState = ((tmp >> 24)&0x7);
   status->pciFunction  = ((tmp >> 16)&0x3);
   status->pciDevice    = ((tmp >>  8)&0xF);
   status->pciBus       = (tmp&0xFF);
}

// Get Lane Status
void PgpCardG2_GetStatus(struct DmaDevice *dev, struct PgpStatus *status, uint8_t lane) {
   uint32_t tempVal;

   struct PgpCardG2Reg *reg;
   reg = (struct PgpCardG2Reg *)dev->reg;

   lane = lane & 0x3;

   memset(status,0,sizeof(struct PgpStatus));
   status->lane = lane;

   // Control reg
   tempVal = ioread32(&(reg->control));
   switch(lane) {
      case 0: status->loopBack = (tempVal >>  4) & 0x1; break;
      case 1: status->loopBack = (tempVal >>  5) & 0x1; break;
      case 2: status->loopBack = (tempVal >>  6) & 0x1; break;
      case 3: status->loopBack = (tempVal >>  7) & 0x1; break;
   }

   // Per lane status
   switch(lane) {
      case  0: tempVal = ioread32(&(reg->pgp0Stat)); break;
      case  1: tempVal = ioread32(&(reg->pgp1Stat)); break;
      case  2: tempVal = ioread32(&(reg->pgp2Stat)); break;
      case  3: tempVal = ioread32(&(reg->pgp3Stat)); break;
      default: tempVal = 0; break;
   }

   status->locLinkReady  = ( tempVal      ) & 0x1;
   status->remLinkReady  = ( tempVal >>  1) & 0x1;
   status->rxReady       = ( tempVal >>  2) & 0x1;
   status->txReady       = ( tempVal >>  3) & 0x1;
   status->rxCount       = ( tempVal >>  4) & 0xF;
   status->cellErrCnt    = ( tempVal >>  8) & 0xF;
   status->linkDownCnt   = ( tempVal >> 12) & 0xF;
   status->linkErrCnt    = ( tempVal >> 16) & 0xF;
   status->fifoErr       = ( tempVal >> 20) & 0x1;

   // Remote and buffer status
   switch(lane) {
      case  0: tempVal = ioread32(&(reg->l0Data)); break;
      case  1: tempVal = ioread32(&(reg->l1Data)); break;
      case  2: tempVal = ioread32(&(reg->l2Data)); break;
      case  3: tempVal = ioread32(&(reg->l3Data)); break;
      default: tempVal = 0; break;
   }

   status->remData       = ( tempVal >>  8) & 0xFF;
   status->remBuffStatus = ( tempVal >> 16) & 0xFF;
}

