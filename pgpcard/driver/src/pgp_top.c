/**
 *-----------------------------------------------------------------------------
 * Title      : Top level module
 * ----------------------------------------------------------------------------
 * File       : pgp_top.c
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
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
#include <pgp_top.h>
#include <dma_common.h>
#include <pgp_gen2.h>
#include <pgp_gen3.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>

// Init Configuration values
int cfgTxCount = 32;
int cfgRxCount = 32;
int cfgSize    = 2097152;
int cfgMode    = BUFF_COHERENT;
int cfgCont    = 1;

// PCI device IDs
static struct pci_device_id PgpCard_Ids[] = {
   { PCI_DEVICE(PCI_VENDOR_ID_SLAC,   PCI_DEVICE_ID_GEN2)   },
   { PCI_DEVICE(PCI_VENDOR_ID_SLAC,   PCI_DEVICE_ID_GEN3)   },
   { 0, }
};

// Module Name
#define MOD_NAME "pgpcard"

MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, PgpCard_Ids);
module_init(PgpCard_Init);
module_exit(PgpCard_Exit);

// PCI driver structure
static struct pci_driver PgpCardDriver = {
  .name     = MOD_NAME,
  .id_table = PgpCard_Ids,
  .probe    = PgpCard_Probe,
  .remove   = PgpCard_Remove,
};

// Init Kernel Module
int32_t PgpCard_Init(void) {

   /* Allocate and clear memory for all devices. */
   memset(gDmaDevices, 0, sizeof(struct DmaDevice)*MAX_DMA_DEVICES);

   pr_info("%s: Init\n",MOD_NAME);

   // Init structures
   gCl = NULL;
   gDmaDevCount = 0;

   // Register driver
   return(pci_register_driver(&PgpCardDriver));
}


// Exit Kernel Module
void PgpCard_Exit(void) {
   pr_info("%s: Exit.\n",MOD_NAME);
   pci_unregister_driver(&PgpCardDriver);
}


// Create and init device
int PgpCard_Probe(struct pci_dev *pcidev, const struct pci_device_id *dev_id) {
   struct DmaDevice *dev;
   struct pci_device_id *id;
   struct hardware_functions *hfunc;

   int32_t x;
   int32_t dummy;

   if ( cfgMode != BUFF_COHERENT && cfgMode != BUFF_STREAM ) {
      pr_warning("%s: Probe: Invalid buffer mode = %i.\n",MOD_NAME,cfgMode);
      return(-1);
   }

   // First check for valid device
   switch ( pcidev->device ) {
      case PCI_DEVICE_ID_GEN2: hfunc = &(PgpCardG2_functions); break;
      case PCI_DEVICE_ID_GEN3: hfunc = &(PgpCardG3_functions); break;
      default:
         pr_warning("%s: Probe: Unkown device.\n",MOD_NAME);
         return (-1);
         break;
   }

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

   // Get Base Address of registers from pci structure.
   dev->baseAddr = pci_resource_start (pcidev, 0);
   dev->baseSize = pci_resource_len (pcidev, 0);

   // Set configuration
   dev->cfgTxCount = cfgTxCount;
   dev->cfgRxCount = cfgRxCount;
   dev->cfgSize    = cfgSize;
   dev->cfgMode    = cfgMode;
   dev->cfgCont    = cfgCont;

   // Get IRQ from pci_dev structure. 
   dev->irq = pcidev->irq;

   // Set device fields
   dev->device = &(pcidev->dev);
   dev->hwFunc = hfunc;

   // Call common dma init function
   return(Dma_Init(dev));
}


// Cleanup device
void  PgpCard_Remove(struct pci_dev *pcidev) {
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

   // Call common dma init function
   Dma_Clean(dev);
   // Disable device
   pci_disable_device(pcidev);

   pr_info("%s: Remove: Driver is unloaded.\n",MOD_NAME);
}

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

