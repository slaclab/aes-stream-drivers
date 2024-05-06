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
#include <data_dev_common.h>
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
int cfgCont    = 1;        // Continuous operation flag

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

/** Exposed to common driver code **/
const char* gModName = MOD_NAME;

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

/** Exposed to common driver code **/
struct pci_driver* gPciDriver = &DataGpuDriver;

/**
 * DataGpu_Init - Initialize the Data Device kernel module
 *
 * This function initializes the Data Device kernel module. See DataDev_Common_Init
 * for the rest of the module init functionality.
 *
 * Return: 0 on success, negative error code on failure.
 */
int32_t DataGpu_Init(void) {
   // Run common init code
   return DataDev_Common_Init();
}

/**
 * DataGpu_Exit - Clean up and exit the Data Device kernel module
 *
 * This function is called when the Data Device kernel module is being removed
 * from the kernel. See DataDev_Common_Exit for the rest of the module exit
 * functionality.
 */
void DataGpu_Exit(void) {
   // Run common exit code
   DataDev_Common_Exit();
}

/**
 * DataGpu_InitCfg - Init device configuration parameters
 * @dev: DMA device to initialize
 * 
 * This is called from DataDev_Common_Probe and should be used to init any
 * device configuration parameters specific to this driver.
 *
 * Return: 0 on success, negative error code on failure
 */
int DataGpu_InitCfg(struct DmaDevice* dev) {
   // Initialize device configuration parameters
   dev->cfgTxCount = cfgTxCount;
   dev->cfgRxCount = cfgRxCount;
   dev->cfgSize    = cfgSize;
   dev->cfgMode    = cfgMode;
   dev->cfgCont    = cfgCont;
   
   return 0;
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
   // Run common driver code with our callback
   return DataDev_Common_Probe(pcidev, dev_id, DataGpu_InitCfg);
}

/**
 * DataGpu_Remove - Clean up and remove a DMA device
 * @pcidev: PCI device structure
 *
 * This function is called by the PCI core when the device is removed from the system.
 * It searches for the device in the global DMA devices array, decrements the global
 * DMA device count, calls the common DMA clean function to free allocated resources,
 * and disables the PCI device.
 */
void DataGpu_Remove(struct pci_dev *pcidev) {
   // Run common driver code
   DataDev_Common_Remove(pcidev);
}

/**
 * DataGpu_SeqShow - Display device information in sequence file
 * @s: sequence file pointer to which the device information is written
 * @dev: device structure containing the data to be displayed
 *
 * This function reads the AXI version from the device and displays it along
 * with other device-specific information using the AxiVersion_Show and
 * AxisG2_SeqShow functions. It's primarily used for proc file outputs,
 * presenting a standardized view of device details for debugging or
 * system monitoring.
 */
void DataGpu_SeqShow(struct seq_file *s, struct DmaDevice *dev) {
   // Run common driver code
   DataDev_Common_SeqShow(s, dev);
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

      // Default handler for other commands not specifically handled above.
      // Delegates to a generic DataDev_Common_Command function for any other commands.
      default:
         return DataDev_Common_Command(dev, cmd, arg);
   }

   // If the command is not recognized, return an error.
   return -1;
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
   .irq          = AxisG2_Irq,             // Handle interrupts.
   .init         = AxisG2_Init,            // Initialize device hardware.
   .clear        = AxisG2_Clear,           // Clear device state or buffers.
   .enable       = AxisG2_Enable,          // Enable device operations.
   .retRxBuffer  = AxisG2_RetRxBuffer,     // Retrieve received buffer.
   .sendBuffer   = AxisG2_SendBuffer,      // Send buffer to device.
   .command      = DataGpu_Command,        // Issue commands to device.
   .seqShow      = DataGpu_SeqShow, // Display device sequence info.
};

/** Exposed to common driver code **/
struct hardware_functions* gHardwareFuncs = &DataGpu_functions;

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

/* Used to determine the device name formatting
 */
module_param(cfgDevName,int,0);
MODULE_PARM_DESC(cfgDevName, "Device Name Formating Setting");
