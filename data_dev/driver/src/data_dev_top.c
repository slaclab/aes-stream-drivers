/**
 * ----------------------------------------------------------------------------
 * Description:
 * This file is part of the aes_stream_drivers package, responsible for defining
 * top-level module types and functions for AES stream device drivers. It
 * includes initialization and cleanup routines for the kernel module, as well
 * as device probing, removal, and command execution functions.
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
int cfgTxCount  = 1024;
int cfgRxCount  = 1024;
int cfgSize     = 0x20000; // 128kB
int cfgMode     = BUFF_COHERENT;
int cfgCont     = 1;
int cfgIrqHold  = 10000;
int cfgIrqDis   = 0;
int cfgBgThold0 = 0;
int cfgBgThold1 = 0;
int cfgBgThold2 = 0;
int cfgBgThold3 = 0;
int cfgBgThold4 = 0;
int cfgBgThold5 = 0;
int cfgBgThold6 = 0;
int cfgBgThold7 = 0;

// Probe failure global flag used in driver init
// function to unregister driver
static int probeReturn = 0;

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

/**
 * struct pci_driver DataDevDriver - Device driver for AES data devices
 * @name: Name of the driver
 * @id_table: PCI device ID table for devices this driver can handle
 * @probe: Callback for device probing. Initializes device instance.
 * @remove: Callback for device removal. Cleans up device instance.
 *
 * This structure defines the PCI driver for AES data devices, handling
 * device initialization, removal, and shutdown. It matches specific PCI
 * devices to this driver using the id_table and provides callbacks for
 * device lifecycle management.
 */
static struct pci_driver DataDevDriver = {
  .name     = MOD_NAME,
  .id_table = DataDev_Ids,
  .probe    = DataDev_Probe,
  .remove   = DataDev_Remove,
};

/**
 * DataDev_Init - Initialize the Data Device kernel module
 *
 * This function initializes the Data Device kernel module. It registers the PCI
 * driver, initializes global variables, and sets up the device configuration.
 * It checks for a probe failure and, if detected, unregisters the driver and
 * returns the error.
 *
 * Return: 0 on success, negative error code on failure.
 */
int32_t DataDev_Init(void) {
   int ret;

   /* Clear memory for all DMA devices */
   memset(gDmaDevices, 0, sizeof(struct DmaDevice) * MAX_DMA_DEVICES);

   pr_info("%s: Init\n", MOD_NAME);

   /* Initialize global variables */
   gCl = NULL;
   gDmaDevCount = 0;

   /* Register PCI driver */
   ret = pci_register_driver(&DataDevDriver);
   if (probeReturn != 0) {
      pr_err("%s: Init: failure detected in init. Unregistering driver.\n", MOD_NAME);
      pci_unregister_driver(&DataDevDriver);
      return probeReturn;
   }

   return ret;
}

/**
 * DataDev_Exit - Clean up and exit the Data Device kernel module
 *
 * This function is called when the Data Device kernel module is being removed
 * from the kernel. It unregisters the PCI driver, effectively cleaning up
 * any resources that were allocated during the operation of the module.
 */
void DataDev_Exit(void) {
   // Log module exit
   pr_info("%s: Exit.\n", MOD_NAME);
   // Unregister the PCI driver to clean up
   pci_unregister_driver(&DataDevDriver);
}

/**
 * DataDev_Probe - Probe for the AES stream device.
 * @pdev: PCI device structure for the device.
 * @id: Entry in data_dev_id table that matches with this device.
 *
 * This function is called by the kernel when a device matching the device ID table
 * is found. It initializes the device, allocates required resources, and registers
 * the device for use. It sets up the device-specific data, prepares the hardware
 * for operation, and initializes any device-specific work structures.
 *
 * Return: 0 on success, negative error code on failure.
 */
int DataDev_Probe(struct pci_dev *pcidev, const struct pci_device_id *dev_id) {
   struct DmaDevice *dev;
   struct pci_device_id *id;
   struct hardware_functions *hfunc;

   int32_t x;
   int32_t axiWidth;
   int ret;

   struct AxisG2Data *hwData;

   if ( cfgMode != BUFF_COHERENT && cfgMode != BUFF_STREAM ) {
      pr_err("%s: Probe: Invalid buffer mode = %i.\n", MOD_NAME, cfgMode);
      return -EINVAL; // Return directly with an error code
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
      pr_err("%s: Probe: Too Many Devices.\n", MOD_NAME);
      return -ENOMEM; // Return directly with an error code
   }
   dev = &gDmaDevices[id->driver_data];
   dev->index = id->driver_data;

   // Create a device name
   ret = snprintf(dev->devName, sizeof(dev->devName), "%s_%i", MOD_NAME, dev->index);
   if (ret < 0 || ret >= sizeof(dev->devName)) {
      pr_err("%s: Probe: Error in snprintf() while formatting device name\n", MOD_NAME);
      return -EINVAL; // Return directly with an error code
   }

   // Enable the device
   ret = pci_enable_device(pcidev);
   if (ret) {
      pr_err("%s: Probe: pci_enable_device() = %i.\n", MOD_NAME, ret);
      return ret; // Return with error code
   }
   pci_set_master(pcidev);

   // Get Base Address of registers from pci structure.
   dev->baseAddr = pci_resource_start (pcidev, 0);
   dev->baseSize = pci_resource_len (pcidev, 0);

   // Remap the I/O register block so that it can be safely accessed.
   if ( Dma_MapReg(dev) < 0 ) {
      /* Cleanup the mess */
      pci_disable_device(pcidev);
      // Exit at end of cleanup
      probeReturn = -ENOMEM;
      return probeReturn;
   }

   // Set configuration
   dev->cfgTxCount    = cfgTxCount;
   dev->cfgRxCount    = cfgRxCount;
   dev->cfgSize       = cfgSize;
   dev->cfgMode       = cfgMode;
   dev->cfgCont       = cfgCont;
   dev->cfgIrqHold    = cfgIrqHold;
   dev->cfgIrqDis     = cfgIrqDis;
   dev->cfgBgThold[0] = cfgBgThold0;
   dev->cfgBgThold[1] = cfgBgThold1;
   dev->cfgBgThold[2] = cfgBgThold2;
   dev->cfgBgThold[3] = cfgBgThold3;
   dev->cfgBgThold[4] = cfgBgThold4;
   dev->cfgBgThold[5] = cfgBgThold5;
   dev->cfgBgThold[6] = cfgBgThold6;
   dev->cfgBgThold[7] = cfgBgThold7;

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
   if ((readl(dev->reg) & 0x10000) != 0) {
      // Get the AXI Address width (in units of bits)
      axiWidth = (readl(dev->reg + 0x34) >> 8) & 0xFF;

      if (!dma_set_mask(dev->device, DMA_BIT_MASK(axiWidth))) {
         dev_info(dev->device, "Init: Using %d-bit DMA mask.\n", axiWidth);

         if (!dma_set_coherent_mask(dev->device, DMA_BIT_MASK(axiWidth))) {
            dev_info(dev->device, "Init: Using %d-bit coherent DMA mask.\n", axiWidth);
         } else {
            dev_warn(dev->device, "Init: Failed to set coherent DMA mask.\n");
         }
      } else {
         dev_warn(dev->device, "Init: Failed to set DMA mask.\n");
      }
   }

   // Call common dma init function
   if ( Dma_Init(dev) < 0 ) {
      /* Cleanup the mess */
      pci_disable_device(pcidev);
      // Exit at end of cleanup
      probeReturn = -ENOMEM;
      return probeReturn;
   }

   // Get hardware data structure
   hwData = (struct AxisG2Data *)dev->hwData;

   dev_info(dev->device,"Init: Reg  space mapped to 0x%llx.\n",(uint64_t)dev->reg);
   dev_info(dev->device,"Init: User space mapped to 0x%llx with size 0x%x.\n",(uint64_t)dev->rwBase,dev->rwSize);
   dev_info(dev->device,"Init: Top Register = 0x%x\n",readl(dev->reg));

   // Increment count only after probe is setup successfully
   gDmaDevCount++;
   probeReturn = 0;
   return(0);
}

/**
 * DataDev_Remove - Clean up and remove a DMA device
 * @pcidev: PCI device structure
 *
 * This function is called by the PCI core when the device is removed from the system.
 * It searches for the device in the global DMA devices array, decrements the global
 * DMA device count, calls the common DMA clean function to free allocated resources,
 * and disables the PCI device.
 */
void DataDev_Remove(struct pci_dev *pcidev) {
   uint32_t x;
   struct DmaDevice *dev = NULL;

   pr_info("%s: Remove: Remove called.\n", MOD_NAME);

   // Look for matching device
   for (x = 0; x < MAX_DMA_DEVICES; x++) {
      if (gDmaDevices[x].baseAddr == pci_resource_start(pcidev, 0)) {
         dev = &gDmaDevices[x];
         break;
      }
   }

   // Device not found
   if (dev == NULL) {
      pr_err("%s: Remove: Device Not Found.\n", MOD_NAME);
      return;
   }

   // Decrement count
   gDmaDevCount--;

   // Call common DMA clean function
   Dma_Clean(dev);

   // Disable device
   pci_disable_device(pcidev);

   pr_info("%s: Remove: Driver is unloaded.\n", MOD_NAME);
}

/**
 * DataDev_Command - Execute a command on the DMA device
 * @dev: pointer to the DmaDevice structure
 * @cmd: the command to be executed
 * @arg: argument to the command, if any
 *
 * Executes a given command on the specified DMA device. The function
 * handles different commands, including reading the AXI version via
 * AVER_Get command and passing any other commands to the AxisG2_Command
 * function for further processing. The function returns the result of
 * the command execution, which could be a success indicator or an error code.
 *
 * Return: the result of the command execution. Returns -1 if the command
 * is not recognized.
 */
int32_t DataDev_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   switch (cmd) {
      case AVER_Get:
         // AXI Version Read
         return AxiVersion_Get(dev, dev->base + AVER_OFF, arg);
         break;

      default:
         // Delegate command to AxisG2 handler
         return AxisG2_Command(dev, cmd, arg);
         break;
   }
   return -1;
}

/**
 * DataDev_SeqShow - Display device information in sequence file
 * @s: sequence file pointer to which the device information is written
 * @dev: device structure containing the data to be displayed
 *
 * This function reads the AXI version from the device and displays it along
 * with other device-specific information using the AxiVersion_Show and
 * AxisG2_SeqShow functions. It's primarily used for proc file outputs,
 * presenting a standardized view of device details for debugging or
 * system monitoring.
 */
void DataDev_SeqShow(struct seq_file *s, struct DmaDevice *dev) {
   struct AxiVersion aVer;

   // Read AXI version from device
   AxiVersion_Read(dev, dev->base + AVER_OFF, &aVer);

   // Display AXI version information
   AxiVersion_Show(s, dev, &aVer);

   // Display additional device-specific information
   AxisG2_SeqShow(s, dev);
}

/**
 * struct hardware_functions - Hardware function pointers for Data Device operations.
 * @irq: Pointer to the IRQ handler function.
 * @init: Pointer to the initialization function for the device.
 * @clear: Pointer to the function to clear device states or buffers.
 * @enable: Pointer to the function to enable the device.
 * @retRxBuffer: Pointer to the function for returning a received buffer.
 * @sendBuffer: Pointer to the function for sending a buffer.
 * @command: Pointer to the function for executing commands on the device.
 * @seqShow: Pointer to the function for adding data to the proc dump.
 *
 * This structure defines a set of function pointers used for interacting
 * with the hardware. Each member represents a specific operation that can
 * be performed on the device, such as initialization, buffer management,
 * and command execution. This allows for hardware-specific implementations
 * of these operations while maintaining a common interface.
 */
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

module_param(cfgIrqDis,int,0);
MODULE_PARM_DESC(cfgIrqDis, "IRQ Disable");

module_param(cfgBgThold0,int,0);
MODULE_PARM_DESC(cfgBgThold0, "Buff Group Threshold 0");

module_param(cfgBgThold1,int,0);
MODULE_PARM_DESC(cfgBgThold1, "Buff Group Threshold 1");

module_param(cfgBgThold2,int,0);
MODULE_PARM_DESC(cfgBgThold2, "Buff Group Threshold 2");

module_param(cfgBgThold3,int,0);
MODULE_PARM_DESC(cfgBgThold3, "Buff Group Threshold 3");

module_param(cfgBgThold4,int,0);
MODULE_PARM_DESC(cfgBgThold4, "Buff Group Threshold 4");

module_param(cfgBgThold5,int,0);
MODULE_PARM_DESC(cfgBgThold5, "Buff Group Threshold 5");

module_param(cfgBgThold6,int,0);
MODULE_PARM_DESC(cfgBgThold6, "Buff Group Threshold 6");

module_param(cfgBgThold7,int,0);
MODULE_PARM_DESC(cfgBgThold7, "Buff Group Threshold 7");
