/**
 *-----------------------------------------------------------------------------

 * ----------------------------------------------------------------------------
 * File       : data_dev_top.c
 * Created    : 2017-03-17
 * ----------------------------------------------------------------------------
 * Description:
 * Top level module types and functions.
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
#include <data_dev_top.h>
#include <AxiVersion.h>
#include <axi_version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/seq_file.h>
#include <linux/signal.h>
#include <linux/pci.h>
#include <axis_gen2.h>

// Init Configuration values
int cfgTxCount = 1024;
int cfgRxCount = 1024;
int cfgSize    = 0x20000; // 128kB
int cfgMode    = BUFF_COHERENT;
int cfgCont    = 1;
int cfgIrqHold = 0;

struct DmaDevice gDmaDevices[MAX_DMA_DEVICES];

// PCI device IDs
static struct pci_device_id DataDev_Ids[] = {
   { PCI_DEVICE(PCI_VENDOR_ID_SLAC,   PCI_DEVICE_ID_DDEV)   },
   { 0, }
};

// Module Name
#define MOD_NAME "datadev"

MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, DataDev_Ids);
module_init(DataDev_Init);
module_exit(DataDev_Exit);

// PCI driver structure
static struct pci_driver DataDevDriver = {
  .name     = MOD_NAME,
  .id_table = DataDev_Ids,
  .probe    = DataDev_Probe,
  .remove   = DataDev_Remove,
};

// Init Kernel Module
int32_t DataDev_Init(void) {

   /* Allocate and clear memory for all devices. */
   memset(gDmaDevices, 0, sizeof(struct DmaDevice)*MAX_DMA_DEVICES);

   pr_info("%s: Init\n",MOD_NAME);

   // Init structures
   gCl = NULL;
   gDmaDevCount = 0;

   // Register driver
   return(pci_register_driver(&DataDevDriver));
}


// Exit Kernel Module
void DataDev_Exit(void) {
   pr_info("%s: Exit.\n",MOD_NAME);
   pci_unregister_driver(&DataDevDriver);
}


// Create and init device
int DataDev_Probe(struct pci_dev *pcidev, const struct pci_device_id *dev_id) {
   struct DmaDevice *dev;
   struct pci_device_id *id;
   struct hardware_functions *hfunc;

   int32_t x;
   int32_t dummy;
   int32_t axiWidth;

   if ( cfgMode != BUFF_COHERENT && cfgMode != BUFF_STREAM ) {
      pr_warning("%s: Probe: Invalid buffer mode = %i.\n",MOD_NAME,cfgMode);
      return(-1);
   }

   // Set hardware functions
   hfunc = &(DataDev_functions);

   id = (struct pci_device_id *) dev_id;

   // We keep device instance number in id->driver_data
   id->driver_data = -1;

   // Find empty structure
   for (x = 0; x < MAX_DMA_DEVICES; x++) {
      if (gDmaDevices[x].baseAddr == 0) {
         id->driver_data = x;
         break;
      }
   }

   // Overflow
   if (id->driver_data < 0) {
      pr_warning("%s: Probe: Too Many Devices.\n",MOD_NAME);
      return (-1);
   }
   dev = &gDmaDevices[id->driver_data];
   dev->index = id->driver_data;

   // Increment count
   gDmaDevCount++;

   // Create a device name
   sprintf(dev->devName,"%s_%i",MOD_NAME,dev->index);

   // Enable the device
   dummy = pci_enable_device(pcidev);
   pci_set_master(pcidev);

   // Get Base Address of registers from pci structure.
   dev->baseAddr = pci_resource_start (pcidev, 0);
   dev->baseSize = pci_resource_len (pcidev, 0);

   // Remap the I/O register block so that it can be safely accessed.
   if ( Dma_MapReg(dev) < 0 ) return(-1);

   // Set configuration
   dev->cfgTxCount = cfgTxCount;
   dev->cfgRxCount = cfgRxCount;
   dev->cfgSize    = cfgSize;
   dev->cfgMode    = cfgMode;
   dev->cfgCont    = cfgCont;
   dev->cfgIrqHold = cfgIrqHold;

   // Get IRQ from pci_dev structure.
   dev->irq = pcidev->irq;

   // Set device fields
   dev->pcidev = pcidev;
   dev->device = &(pcidev->dev);
   dev->hwFunc = hfunc;

   dev->reg    = dev->base + AGEN2_OFF;
   dev->rwBase = dev->base + PHY_OFF;
   dev->rwSize = (2*USER_SIZE) - PHY_OFF;

   // Set and clear reset
   dev_info(dev->device,"Init: Setting user reset\n");
   AxiVersion_SetUserReset(dev->base + AVER_OFF,true);
   dev_info(dev->device,"Init: Clearing user reset\n");
   AxiVersion_SetUserReset(dev->base + AVER_OFF,false);

   // 128bit desc, = 64-bit address map
   if ( (ioread32(dev->reg) & 0x10000) != 0) {
      // Get the AXI Address width (in units of bits)
      axiWidth = (ioread32(dev->reg+0x34) >> 8) & 0xFF;
      if ( !dma_set_mask_and_coherent(dev->device, DMA_BIT_MASK(axiWidth)) ) {
         dev_info(dev->device,"Init: Using %d-bit DMA mask.\n",axiWidth);
      } else {
         dev_warn(dev->device,"Init: Failed to set DMA mask.\n");
      }
   }

   // Call common dma init function
   if ( Dma_Init(dev) < 0 ) return(-1);

   dev_info(dev->device,"Init: Reg  space mapped to 0x%llx.\n",(uint64_t)dev->reg);
   dev_info(dev->device,"Init: User space mapped to 0x%llx with size 0x%x.\n",(uint64_t)dev->rwBase,dev->rwSize);
   dev_info(dev->device,"Init: Top Register = 0x%x\n",ioread32(dev->reg));

   return(0);
}

// Cleanup device
void  DataDev_Remove(struct pci_dev *pcidev) {
   uint32_t x;

   struct DmaDevice *dev = NULL;

   pr_info("%s: Remove: Remove called.\n",MOD_NAME);

   // Look for matching device
   for (x = 0; x < MAX_DMA_DEVICES; x++) {
      if ( gDmaDevices[x].baseAddr == pci_resource_start(pcidev, 0)) {
         dev = &gDmaDevices[x];
         break;
      }
   }

   // Device not found
   if (dev == NULL) {
      pr_err("%s: Remove: Device Not Found.\n",MOD_NAME);
      return;
   }

   // Decrement count
   gDmaDevCount--;

   // Disable device
   pci_disable_device(pcidev);

   // Call common dma init function
   Dma_Clean(dev);
   pr_info("%s: Remove: Driver is unloaded.\n",MOD_NAME);
}

// Execute command
int32_t DataDev_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   switch (cmd) {

      // AXI Version Read
      case AVER_Get:
         return(AxiVersion_Get(dev,dev->base + AVER_OFF, arg));
         break;

      default:
         return(AxisG2_Command(dev,cmd,arg));
         break;
   }
   return(-1);
}

// Add data to proc dump
void DataDev_SeqShow(struct seq_file *s, struct DmaDevice *dev) {
   struct AxiVersion aVer;
   AxiVersion_Read(dev, dev->base + AVER_OFF, &aVer);
   AxiVersion_Show(s, dev, &aVer);
   AxisG2_SeqShow(s,dev);
}

// Set functions
struct hardware_functions DataDev_functions = {
   .irq          = AxisG2_Irq,
   .init         = AxisG2_Init,
   .clear        = AxisG2_Clear,
   .enable       = AxisG2_Enable,
   .retRxBuffer  = AxisG2_RetRxBuffer,
   .sendBuffer   = AxisG2_SendBuffer,
   .command      = DataDev_Command,
   .seqShow      = DataDev_SeqShow,
};

// Parameters
module_param(cfgTxCount,int,0);
MODULE_PARM_DESC(cfgTxCount, "TX buffer count");

module_param(cfgRxCount,int,0);
MODULE_PARM_DESC(cfgRxCount, "RX buffer count");

module_param(cfgSize,int,0);
MODULE_PARM_DESC(cfgSize, "Rx/TX Buffer size");

module_param(cfgMode,int,0);
MODULE_PARM_DESC(cfgMode, "RX buffer mode");

module_param(cfgCont,int,0);
MODULE_PARM_DESC(cfgCont, "RX continue enable");

module_param(cfgIrqHold,int,0);
MODULE_PARM_DESC(cfgIrqHold, "IRQ Holdoff");

