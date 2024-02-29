/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    This file is part of the aes_stream_drivers package, responsible for defining
 *    top-level module types and functions for AES stream device drivers. It
 *    includes initialization and cleanup routines for the kernel module, as well
 *    as device probing, removal, and command execution functions.
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

#include <data_gpu_top.h>
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
#include <GpuAsync.h>
#include <gpu_async.h>

/*
 * Configuration values initialization.
 * These values are used to set up the DMA (Direct Memory Access) transfer settings.
 */
int cfgTxCount = 1024;  // Transmit buffer count
int cfgRxCount = 1024;  // Receive buffer count
int cfgSize    = 0x20000; // Size of the buffer: 128kB
int cfgMode    = BUFF_COHERENT; // Buffer mode: coherent
int cfgCont    = 1;        // Continuous operation flag

/*
 * Global array of DMA devices.
 * This array holds the configuration and status of each DMA device handled by this driver.
 */
struct DmaDevice gDmaDevices[MAX_DMA_DEVICES];

/*
 * PCI device identification array.
 * This array is used by the kernel to match this driver to the specific hardware based on PCI IDs.
 */
static struct pci_device_id DataGpu_Ids[] = {
   { PCI_DEVICE(PCI_VENDOR_ID_SLAC, PCI_DEVICE_ID_DDEV) }, // Device ID for SLAC vendor, specific device
   { 0, } // Terminator for the ID list
};

/* Module metadata definitions */
#define MOD_NAME "datagpu" // Name of the module
MODULE_LICENSE("GPL"); // License type: GPL for open source compliance
MODULE_DEVICE_TABLE(pci, DataGpu_Ids); // Associate the PCI ID table with this module
module_init(DataGpu_Init); // Initialize the module with DataGpu_Init function
module_exit(DataGpu_Exit); // Clean-up the module with DataGpu_Exit function

/*
 * PCI driver structure definition.
 * This structure contains callback functions and device IDs for the DataGpu driver.
 */
static struct pci_driver DataGpuDriver = {
   .name     = MOD_NAME,    // Name of the driver
   .id_table = DataGpu_Ids, // PCI device ID table
   .probe    = DataGpu_Probe,  // Probe function for device discovery
   .remove   = DataGpu_Remove, // Remove function for device disconnection
};

/**
 * DataGpu_Init - Initialize the DataGpu Kernel Module.
 *
 * This function initializes the Data GPU kernel module. It allocates and clears
 * memory for all DMA devices managed by this module, initializes global variables,
 * and registers the module with the PCI subsystem as a driver for specified devices.
 *
 * Return: Zero on success, negative error code on failure.
 */
int32_t DataGpu_Init(void) {
   /* Clear memory for all DMA devices to reset their configuration. */
   memset(gDmaDevices, 0, sizeof(struct DmaDevice) * MAX_DMA_DEVICES);

   /* Log module initialization with the kernel logging system. */
   pr_info("%s: Init\n", MOD_NAME);

   /* Initialize global pointers and counters. */
   gCl = NULL;             // Clear global class pointer.
   gDmaDevCount = 0;       // Reset the count of DMA devices managed by this module.

   /* Register this driver with the PCI subsystem to handle devices matching our IDs. */
   return pci_register_driver(&DataGpuDriver);
}

/**
 * DataGpu_Exit - Clean up the DataGpu Kernel Module.
 *
 * This function is called when the DataGpu kernel module is being removed
 * from the system. It unregisters the driver from the PCI subsystem and
 * logs the module exit with the kernel logging system.
 */
void DataGpu_Exit(void) {
   /* Log module exit with the kernel logging system. */
   pr_info("%s: Exit.\n", MOD_NAME);

   /* Unregister this driver from the PCI subsystem. */
   pci_unregister_driver(&DataGpuDriver);
}

/**
 * DataGpu_Probe - Probe function for DataGpu devices.
 * @pcidev: PCI device structure provided by the kernel.
 * @dev_id: PCI device ID that matches this driver.
 *
 * This function is called by the kernel when a device that matches the ID table
 * for this driver is found. It initializes the device, sets up DMA and other
 * hardware functionalities, and prepares the device for use.
 *
 * Return: 0 on success, negative error code on failure.
 */
int DataGpu_Probe(struct pci_dev *pcidev, const struct pci_device_id *dev_id) {
   struct DmaDevice *dev;
   struct pci_device_id *id;
   struct hardware_functions *hfunc;

   int32_t x;
   int32_t dummy;
   int32_t axiWidth;

   if ( cfgMode != BUFF_COHERENT && cfgMode != BUFF_STREAM ) {
      pr_warn("%s: Probe: Invalid buffer mode = %i.\n",MOD_NAME,cfgMode);
      return(-1);
   }

   // Set hardware functions
   hfunc = &(DataGpu_functions);

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
      pr_warn("%s: Probe: Too Many Devices.\n",MOD_NAME);
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

   // Get IRQ from pci_dev structure.
   dev->irq = pcidev->irq;

   // Set device fields
   dev->pcidev = pcidev;
   dev->device = &(pcidev->dev);
   dev->hwFunc = hfunc;

   dev->reg    = dev->base + AGEN2_OFF;
   dev->rwBase = dev->base + PHY_OFF;
   dev->rwSize = (2*USER_SIZE) - PHY_OFF;

   // GPU Init
   Gpu_Init(dev, GPU_OFF);

   // Set and clear reset
   dev_info(dev->device,"Init: Setting user reset\n");
   AxiVersion_SetUserReset(dev->base + AVER_OFF,true);
   dev_info(dev->device,"Init: Clearing user reset\n");
   AxiVersion_SetUserReset(dev->base + AVER_OFF,false);

   // 128bit desc, = 64-bit address map
   if ( (readl(dev->reg) & 0x10000) != 0) {
      // Get the AXI Address width (in units of bits)
      axiWidth = (readl(dev->reg+0x34) >> 8) & 0xFF;
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
   dev_info(dev->device,"Init: Top Register = 0x%x\n",readl(dev->reg));

   return(0);
}

/**
 * DataGpu_Remove - Clean up resources for a DataGpu device.
 * @pcidev: Pointer to the PCI device structure.
 *
 * This function is called by the PCI subsystem to remove a device (usually on module unload
 * or when the device is physically removed from the system). It searches for the device
 * within the global DMA devices array, disables the PCI device, cleans up DMA resources,
 * and logs the removal.
 */
void DataGpu_Remove(struct pci_dev *pcidev) {
   uint32_t x;
   struct DmaDevice *dev = NULL;

   /* Log the call to remove the device. */
   pr_info("%s: Remove: Remove called.\n", MOD_NAME);

   /* Search for the device in the global DMA devices array. */
   for (x = 0; x < MAX_DMA_DEVICES; x++) {
      if (gDmaDevices[x].baseAddr == pci_resource_start(pcidev, 0)) {
         dev = &gDmaDevices[x];
         break;
      }
   }

   /* If the device is not found, log an error and exit. */
   if (dev == NULL) {
      pr_err("%s: Remove: Device Not Found.\n", MOD_NAME);
      return;
   }

   /* Decrement the global count of DMA devices. */
   gDmaDevCount--;

   /* Disable the PCI device to prevent further access. */
   pci_disable_device(pcidev);

   /* Clean up DMA resources specific to this device. */
   Dma_Clean(dev);

   /* Log the successful removal of the device. */
   pr_info("%s: Remove: Driver is unloaded.\n", MOD_NAME);
}

/**
 * DataGpu_Command - Execute specific commands on a DMA device.
 *
 * This function handles various commands directed towards DMA devices, specifically
 * targeting GPU-related operations, AXI version reading, and other device-specific commands.
 * Depending on the command, it delegates the action to the appropriate handler function.
 *
 * @dev: Pointer to the DMA device on which the command is to be executed.
 * @cmd: The command code that specifies the action to be taken.
 * @arg: An argument associated with the command, which can be an address, value, or identifier.
 *
 * Return: Zero on success, negative error code on failure, or positive return values
 *         specific to the command executed.
 */
int32_t DataGpu_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   switch (cmd) {
      // GPU Commands
      // Handles adding or removing Nvidia memory based on the command specified.
      case GPU_Add_Nvidia_Memory:
      case GPU_Rem_Nvidia_Memory:
         return Gpu_Command(dev, cmd, arg);

      // AXI Version Read
      // Reads the AXI version from a specified offset within the device.
      case AVER_Get:
         return AxiVersion_Get(dev, dev->base + AVER_OFF, arg);

      // Default handler for other commands not specifically handled above.
      // Delegates to a generic AxisG2_Command function for any other commands.
      default:
         return AxisG2_Command(dev, cmd, arg);
   }

   // If the command is not recognized, return an error.
   return -1;
}

/**
 * DataGpu_SeqShow - Display device information in the proc file system.
 *
 * This function is responsible for adding device-specific data to the proc file
 * system, allowing users to read device information such as version details and
 * other status information via the `/proc` interface. It reads the AXI version
 * information from the device and displays it, along with other device-specific
 * information.
 *
 * @s: Pointer to the seq_file structure, used by the kernel to manage sequence files.
 * @dev: Pointer to the DmaDevice structure, representing the device whose information
 *       is to be displayed.
 *
 * This function does not return a value.
 */
void DataGpu_SeqShow(struct seq_file *s, struct DmaDevice *dev) {
   struct AxiVersion aVer;

   /* Read AXI version information from the device. */
   AxiVersion_Read(dev, dev->base + AVER_OFF, &aVer);

   /* Display the AXI version information. */
   AxiVersion_Show(s, dev, &aVer);

   /* Display additional device-specific information. */
   AxisG2_SeqShow(s, dev);
}

/**
 * struct hardware_functions - Define hardware-specific operations for DataGpu.
 *
 * This structure maps hardware operation callbacks specific to the DataGpu device.
 * Each field represents a pointer to a function that implements a specific aspect
 * of the device's operation, such as initialization, interrupt handling, buffer
 * management, and device-specific commands.
 *
 * @irq: Interrupt handler for the device.
 * @init: Initialize the device.
 * @clear: Clear the device state or buffers.
 * @enable: Enable or disable the device.
 * @retRxBuffer: Retrieve a received buffer from the device.
 * @sendBuffer: Send a buffer to the device.
 * @command: Send a command to the device.
 * @seqShow: Show device sequence information (for debugging or status reports).
 */
struct hardware_functions DataGpu_functions = {
   .irq          = AxisG2_Irq,          // Handle interrupts.
   .init         = AxisG2_Init,         // Initialize device hardware.
   .clear        = AxisG2_Clear,        // Clear device state or buffers.
   .enable       = AxisG2_Enable,       // Enable device operations.
   .retRxBuffer  = AxisG2_RetRxBuffer,  // Retrieve received buffer.
   .sendBuffer   = AxisG2_SendBuffer,   // Send buffer to device.
   .command      = DataGpu_Command,     // Issue commands to device.
   .seqShow      = DataGpu_SeqShow,     // Display device sequence info.
};

/**
 * Module parameters definition and description.
 *
 * This section declares module parameters which can be passed at module load time.
 * It allows for dynamic configuration of the module's behavior without recompilation.
 */

/* TX buffer count parameter
 * Allows setting the number of transmit buffers used by the module.
 * This parameter can be modified at module load time.
 */
module_param(cfgTxCount, int, 0);
MODULE_PARM_DESC(cfgTxCount, "TX buffer count: Number of transmit buffers.");

/* RX buffer count parameter
 * Allows setting the number of receive buffers used by the module.
 * This parameter can be modified at module load time.
 */
module_param(cfgRxCount, int, 0);
MODULE_PARM_DESC(cfgRxCount, "RX buffer count: Number of receive buffers.");

/* Rx/TX buffer size parameter
 * Allows setting the size of both receive and transmit buffers.
 * This parameter can be modified at module load time.
 */
module_param(cfgSize, int, 0);
MODULE_PARM_DESC(cfgSize, "Rx/TX Buffer size: Size of receive and transmit buffers.");

/* RX buffer mode parameter
 * Allows setting the mode of receive buffers.
 * This parameter can be modified at module load time.
 */
module_param(cfgMode, int, 0);
MODULE_PARM_DESC(cfgMode, "RX buffer mode: Mode of the receive buffers.");

/* RX continue enable parameter
 * Enables or disables the continuous receive mode.
 * This parameter can be modified at module load time.
 */
module_param(cfgCont, int, 0);
MODULE_PARM_DESC(cfgCont, "RX continue enable: Enable/disable continuous receive mode.");
