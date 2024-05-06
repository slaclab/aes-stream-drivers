/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    This file implements common functionality of the data_dev and data_gpu
 *    drivers. The goal here is to reduce the amount of duplicated code between
 *    the two drivers.
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

#include <data_dev_common.h>
#include <axi_version.h>
#include <axis_gen2.h>

/** Common device configuration parameters **/
int cfgMode     = BUFF_COHERENT;
int cfgDevName  = 0;

/**
 * Used in DataDev_Common_Init to detect failure of Probe and unregister the driver.
 */
static int probeReturn = 0;

/*
 * Global array of DMA devices.
 * This array holds the configuration and status of each DMA device handled by this driver.
 */
struct DmaDevice gDmaDevices[MAX_DMA_DEVICES];

/**
 * DataDev_Common_Init - Initialize the Data Device kernel module
 *
 * This function initializes the Data Device kernel module. It registers the PCI
 * driver, initializes global variables, and sets up the device configuration.
 * It checks for a probe failure and, if detected, unregisters the driver and
 * returns the error.
 *
 * Return: 0 on success, negative error code on failure.
 */
int32_t DataDev_Common_Init(void) {
   int ret;

   /* Clear memory for all DMA devices */
   memset(gDmaDevices, 0, sizeof(struct DmaDevice) * MAX_DMA_DEVICES);

   pr_info("%s: Init\n", gModName);

   /* Initialize global variables */
   gCl = NULL;
   gDmaDevCount = 0;

   /* Register PCI driver */
   ret = pci_register_driver(gPciDriver);
   if (probeReturn != 0) {
      pr_err("%s: Init: failure detected in init. Unregistering driver.\n", gModName);
      pci_unregister_driver(gPciDriver);
      return probeReturn;
   }

   return ret;
}

/**
 * DataDev_Common_Exit - Clean up and exit the Data Device kernel module
 *
 * This function is called when the Data Device kernel module is being removed
 * from the kernel. It unregisters the PCI driver, effectively cleaning up
 * any resources that were allocated during the operation of the module.
 */
void DataDev_Common_Exit(void) {
   // Log module exit
   pr_info("%s: Exit.\n", gModName);
   // Unregister the PCI driver to clean up
   pci_unregister_driver(gPciDriver);
}

/**
 * DataDev_Common_Remove - Clean up and remove a DMA device
 * @pcidev: PCI device structure
 *
 * This function is called by the PCI core when the device is removed from the system.
 * It searches for the device in the global DMA devices array, decrements the global
 * DMA device count, calls the common DMA clean function to free allocated resources,
 * and disables the PCI device.
 */
void DataDev_Common_Remove(struct pci_dev *pcidev) {
   uint32_t x;
   struct DmaDevice *dev = NULL;

   pr_info("%s: Remove: Remove called.\n", gModName);

   // Look for matching device
   for (x = 0; x < MAX_DMA_DEVICES; x++) {
      if (gDmaDevices[x].baseAddr == pci_resource_start(pcidev, 0)) {
         dev = &gDmaDevices[x];
         break;
      }
   }

   // Device not found
   if (dev == NULL) {
      pr_err("%s: Remove: Device Not Found.\n", gModName);
      return;
   }

   // Decrement count
   gDmaDevCount--;

   // Call common DMA clean function
   Dma_Clean(dev);

   // Disable device
   pci_disable_device(pcidev);

   pr_info("%s: Remove: Driver is unloaded.\n", gModName);
}

/**
 * DataDev_Common_SeqShow - Display device information in sequence file
 * @s: sequence file pointer to which the device information is written
 * @dev: device structure containing the data to be displayed
 *
 * This function reads the AXI version from the device and displays it along
 * with other device-specific information using the AxiVersion_Show and
 * AxisG2_SeqShow functions. It's primarily used for proc file outputs,
 * presenting a standardized view of device details for debugging or
 * system monitoring.
 */
void DataDev_Common_SeqShow(struct seq_file *s, struct DmaDevice *dev) {
   struct AxiVersion aVer;

   // Read AXI version from device
   AxiVersion_Read(dev, dev->base + AVER_OFF, &aVer);

   // Display AXI version information
   AxiVersion_Show(s, dev, &aVer);

   // Display additional device-specific information
   AxisG2_SeqShow(s, dev);
}

/**
 * DataDev_Common_Command - Execute a command on the DMA device
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
int32_t DataDev_Common_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
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
 * DataDev_Common_Probe - Probe for the AES stream device.
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
int DataDev_Common_Probe(struct pci_dev *pcidev, const struct pci_device_id *dev_id, Probe_Init_Cfg initCfg) {
   struct DmaDevice *dev;
   struct pci_device_id *id;
   //struct hardware_functions *hfunc;

   int32_t x;
   int32_t axiWidth;
   int ret;

   struct AxisG2Data *hwData;

   // Validate buffer mode configuration
   if ( cfgMode != BUFF_COHERENT && cfgMode != BUFF_STREAM ) {
      pr_err("%s: Probe: Invalid buffer mode = %i.\n", gModName, cfgMode);
      return -EINVAL; // Return directly with an error code
   }

   // Initialize hardware function pointers
   //hfunc = &(DataDev_functions);

   // Cast device ID for further operations
   id = (struct pci_device_id *) dev_id;

   // Initialize driver data to indicate an empty slot
   id->driver_data = -1;

   // Search for an available device slot
   for (x = 0; x < MAX_DMA_DEVICES; x++) {
      if (gDmaDevices[x].baseAddr == 0) {
         id->driver_data = x;
         break;
      }
   }

   // Check for device slots overflow
   if (id->driver_data < 0) {
      pr_err("%s: Probe: Too Many Devices.\n", gModName);
      return -ENOMEM; // Return directly with an error code
   }
   dev = &gDmaDevices[id->driver_data];
   dev->index = id->driver_data;

   // Attempt to compose a unique device name based on configuration
   if (cfgDevName != 0) {
      // Utilize the PCI device bus number for unique device naming
      // Helpful when multiple PCIe cards are installed in the same server
      ret = snprintf(dev->devName, sizeof(dev->devName), "%s_%02x", gModName, pcidev->bus->number);
   } else {
      // Default to sequential naming based on the device's index
      // Ensures uniqueness in a single card scenario
      ret = snprintf(dev->devName, sizeof(dev->devName), "%s_%i", gModName, dev->index);
   }
   if (ret < 0 || ret >= sizeof(dev->devName)) {
      pr_err("%s: Probe: Error in snprintf() while formatting device name\n", gModName);
      probeReturn = -EINVAL;
      goto err_pre_en;        // Bail out, but clean up first
   }

   // Activate the PCI device
   ret = pci_enable_device(pcidev);
   if (ret) {
      pr_err("%s: Probe: pci_enable_device() = %i.\n", gModName, ret);
      probeReturn = ret;      // Return directly with error code
      goto err_pre_en;        // Bail out, but clean up first
   }
   pci_set_master(pcidev); // Set the device as bus master

   // Retrieve and store the base address and size of the device's register space
   dev->baseAddr = pci_resource_start (pcidev, 0);
   dev->baseSize = pci_resource_len (pcidev, 0);

   // Map the device's register space for use in the driver
   if ( Dma_MapReg(dev) < 0 ) {
      probeReturn = -ENOMEM; // Memory allocation error
      goto err_post_en;
   }

   // Initialize common device configuration parameters
   dev->cfgMode       = cfgMode;       // Operation mode

   // Run driver-specific init function
   if (initCfg && (ret = initCfg(dev)) < 0) {
      pr_err("%s: Probe: initCfg = %i\n", gModName, ret);
      goto err_post_en;
   }

   // Assign the IRQ number from the pci_dev structure
   dev->irq = pcidev->irq;

   // Set basic device context
   dev->pcidev = pcidev;               // PCI device structure
   dev->device = &(pcidev->dev);       // Device structure
   dev->hwFunc = gHardwareFuncs;       // Hardware function pointer

   // Initialize device memory regions
   dev->reg    = dev->base + AGEN2_OFF;   // Register base address
   dev->rwBase = dev->base + PHY_OFF;     // Read/Write base address
   dev->rwSize = (2*USER_SIZE) - PHY_OFF; // Read/Write region size

   // Manage device reset cycle
   dev_info(dev->device,"Init: Setting user reset\n");
   AxiVersion_SetUserReset(dev->base + AVER_OFF,true); // Set user reset
   dev_info(dev->device,"Init: Clearing user reset\n");
   AxiVersion_SetUserReset(dev->base + AVER_OFF,false); // Clear user reset

   // Configure DMA based on AXI address width: 128bit desc, = 64-bit address map
   if ((readl(dev->reg) & 0x10000) != 0) {
      axiWidth = (readl(dev->reg + 0x34) >> 8) & 0xFF; // Extract AXI address width

      // Attempt to set DMA and coherent DMA masks based on AXI width
      if (!dma_set_mask(dev->device, DMA_BIT_MASK(axiWidth))) {
         dev_info(dev->device, "Init: Using %d-bit DMA mask.\n", axiWidth);

         if (!dma_set_coherent_mask(dev->device, DMA_BIT_MASK(axiWidth))) {
            dev_info(dev->device, "Init: Using %d-bit coherent DMA mask.\n", axiWidth);
         } else {
            dev_err(dev->device, "Init: Failed to set coherent DMA mask.\n");
            probeReturn = -EINVAL;
            goto err_post_en;
         }
      } else {
         dev_err(dev->device, "Init: Failed to set DMA mask.\n");
         probeReturn = -EINVAL;
         goto err_post_en;
      }
   }

   // Initialize common DMA functionalities
   if (Dma_Init(dev) < 0) {
      probeReturn = -ENOMEM;      // Indicate memory allocation error
      goto err_post_en;
   }

   // Get hardware data structure
   hwData = (struct AxisG2Data *)dev->hwData;

   // Log memory mapping information
   dev_info(dev->device,"Init: Reg space mapped to 0x%p.\n",dev->reg);
   dev_info(dev->device,"Init: User space mapped to 0x%p with size 0x%x.\n",dev->rwBase,dev->rwSize);
   dev_info(dev->device,"Init: Top Register = 0x%x\n",readl(dev->reg));

   // Finalize device probe successfully
   gDmaDevCount++;                   // Increment global device count
   probeReturn = 0;                  // Set successful return code
   return probeReturn;               // Return success

err_post_en:
   pci_disable_device(pcidev);      // Disable PCI device on failure
err_pre_en:
   memset(dev, 0, sizeof(*dev));    // Clear out the slot we took in gDmaDevices
   return probeReturn;
}