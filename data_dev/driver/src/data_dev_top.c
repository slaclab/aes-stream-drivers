/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
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

// Init Configuration values
int cfgTxCount  = 1024;
int cfgRxCount  = 1024;
int cfgSize     = 0x20000; // 128kB
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

// PCI device IDs
static struct pci_device_id DataDev_Ids[] = {
   { PCI_DEVICE(PCI_VENDOR_ID_SLAC,   PCI_DEVICE_ID_DDEV)   },
   { 0, }
};

// Module Name
#define MOD_NAME "datadev"
const char* gModName = MOD_NAME;

MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, DataDev_Ids);
module_init(DataDev_Init);
module_exit(DataDev_Exit);

/**
 * DataDev_Init - Initialize the Data Device kernel module
 *
 * This function initializes the Data Device kernel module. See DataDev_Common_Init
 * for the rest of the module init functionality.
 *
 * Return: 0 on success, negative error code on failure.
 */
int32_t DataDev_Init(void) {
   // Run common init code
   return DataDev_Common_Init();
}

/**
 * DataDev_Exit - Clean up and exit the Data Device kernel module
 *
 * This function is called when the Data Device kernel module is being removed
 * from the kernel. See DataDev_Common_Exit for the rest of the module exit
 * functionality.
 */
void DataDev_Exit(void) {
   // Run common exit code
   return DataDev_Common_Exit();
}

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

/** Exposed to common driver code **/
struct pci_driver* gPciDriver = &DataDevDriver;

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
   // Run common driver code
   DataDev_Common_Remove(pcidev);
}

/**
 * DataDev_Init_DeviceCfg - Driver-specific configuration initialization
 * @dev: DmaDevice to initialize
 *
 * This function simply handles copying module parameters into the DmaDevice.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int DataDev_Init_DeviceCfg(struct DmaDevice* dev) {
   // cfgMode and cfgDevName handled by DataDev_Common_Probe
   dev->cfgTxCount    = cfgTxCount;    // Transmit buffer count
   dev->cfgRxCount    = cfgRxCount;    // Receive buffer count
   dev->cfgSize       = cfgSize;       // Configuration size
   dev->cfgCont       = cfgCont;       // Continuous operation flag
   dev->cfgIrqHold    = cfgIrqHold;    // IRQ hold configuration
   dev->cfgIrqDis     = cfgIrqDis;     // IRQ disable flag
   dev->cfgBgThold[0] = cfgBgThold0;   // Background threshold 0
   dev->cfgBgThold[1] = cfgBgThold1;   // Background threshold 1
   dev->cfgBgThold[2] = cfgBgThold2;   // Background threshold 2
   dev->cfgBgThold[3] = cfgBgThold3;   // Background threshold 3
   dev->cfgBgThold[4] = cfgBgThold4;   // Background threshold 4
   dev->cfgBgThold[5] = cfgBgThold5;   // Background threshold 5
   dev->cfgBgThold[6] = cfgBgThold6;   // Background threshold 6
   dev->cfgBgThold[7] = cfgBgThold7;   // Background threshold 7
   return 0;
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
 * In this case, it calls into DataDev_Common_Probe which handles the common setup.
 *
 * Return: 0 on success, negative error code on failure.
 */
int DataDev_Probe(struct pci_dev *pcidev, const struct pci_device_id *dev_id) {
   return DataDev_Common_Probe(pcidev, dev_id, DataDev_Init_DeviceCfg);
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
   .seqShow      = DataDev_Common_SeqShow,
};

/** Exposed to common driver code **/
struct hardware_functions* gHardwareFuncs = &DataDev_functions;

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
   // Run common driver code
   return DataDev_Common_Command(dev, cmd, arg);
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
   // Run common driver code
   DataDev_Common_SeqShow(s, dev);
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

module_param(cfgDevName,int,0);
MODULE_PARM_DESC(cfgDevName, "Device Name Formating Setting");
