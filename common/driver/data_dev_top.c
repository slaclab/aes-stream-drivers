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
#include <linux/slab.h>
#include <axis_gen2.h>
#include <GpuAsync.h>

#ifdef DATA_GPU
#include <GpuAsyncRegs.h>
#include <gpu_async.h>
#endif

// PCI_IRQ_LEGACY was renamed to PCI_IRQ_INTX in Linux 6.11; the old name was
// kept as a deprecated alias for a few releases, then dropped. Provide the new
// spelling on pre-6.11 kernels so the single pci_alloc_irq_vectors() call site
// builds across the CI matrix (Ubuntu 22.04 / kernel 5.15 lacks PCI_IRQ_INTX).
#ifndef PCI_IRQ_INTX
#define PCI_IRQ_INTX PCI_IRQ_LEGACY
#endif

// Init Configuration values
static int cfgTxCount  = 1024;
static int cfgRxCount  = 1024;
static int cfgSize     = 0x20000;  // 128kB
static int cfgMode     = BUFF_COHERENT;
static int cfgCont     = 1;
static int cfgIrqHold  = 10000;
static int cfgIrqDis   = 0;
static int cfgBgThold0 = 0;
static int cfgBgThold1 = 0;
static int cfgBgThold2 = 0;
static int cfgBgThold3 = 0;
static int cfgBgThold4 = 0;
static int cfgBgThold5 = 0;
static int cfgBgThold6 = 0;
static int cfgBgThold7 = 0;
static int cfgDevName  = 0;
static int cfgTimeout  = 0xFFFF;
static int cfgDebug    = 0;

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

   pr_info("%s: aes-stream-drivers %s\n", MOD_NAME, GITV);

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
   uint32_t axiWidth;
   int ret;

   // Validate buffer mode configuration
   if ( cfgMode != BUFF_COHERENT && cfgMode != BUFF_STREAM ) {
      pr_err("%s: Probe: Invalid buffer mode = %i.\n", MOD_NAME, cfgMode);
      return -EINVAL;  // Return directly with an error code
   }

   // Initialize hardware function pointers
   hfunc = &(DataDev_functions);

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
      pr_err("%s: Probe: Too Many Devices.\n", MOD_NAME);
      return -ENOMEM;  // Return directly with an error code
   }
   dev = &gDmaDevices[id->driver_data];
   dev->index = id->driver_data;

   // Attempt to compose a unique device name based on configuration
   if (cfgDevName != 0) {
      // Utilize the PCI device bus number for unique device naming
      // Helpful when multiple PCIe cards are installed in the same server
      ret = snprintf(dev->devName, sizeof(dev->devName), "%s_%02x", MOD_NAME, pcidev->bus->number);//NOLINT
   } else {
      // Default to sequential naming based on the device's index
      // Ensures uniqueness in a single card scenario
      ret = snprintf(dev->devName, sizeof(dev->devName), "%s_%i", MOD_NAME, dev->index);//NOLINT
   }
   if (ret < 0 || ret >= sizeof(dev->devName)) {
      pr_err("%s: Probe: Error in snprintf() while formatting device name\n", MOD_NAME);
      probeReturn = -EINVAL;
      goto err_pre_en;  // Bail out, but clean up first
   }

   // Activate the PCI device
   ret = pci_enable_device(pcidev);
   if (ret) {
      dev_err(&pcidev->dev, "%s: Probe: pci_enable_device() = %i.\n", MOD_NAME, ret);
      probeReturn = ret;  // Return directly with error code
      goto err_pre_en;    // Bail out, but clean up first
   }
   pci_set_master(pcidev);  // Set the device as bus master

   // Retrieve and store the base address and size of the device's register space
   dev->baseAddr = pci_resource_start(pcidev, 0);
   dev->baseSize = pci_resource_len(pcidev, 0);

   // Early out if we have an invalid BAR
   if ( dev->baseAddr == 0 ) {
      dev_err(&pcidev->dev, "Init: failed to get pci base address; check your BAR0 assignment!\n");
      probeReturn = 0;  // Allow cards with valid BAR to load
      goto err_post_en;
   }

   // Set basic device attributes
   dev->pcidev = pcidev;          // PCI device structure
   dev->device = &(pcidev->dev);  // Device structure

   // Map the device's register space for use in the driver
   if ( Dma_MapReg(dev) < 0 ) {
      probeReturn = -ENOMEM;  // Memory allocation error
      goto err_post_en;
   }

   // Initialize device configuration parameters
   dev->cfgTxCount    = cfgTxCount;    // Transmit buffer count
   dev->cfgRxCount    = cfgRxCount;    // Receive buffer count
   dev->cfgSize       = cfgSize;       // Configuration size
   dev->cfgMode       = cfgMode;       // Operation mode
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
   dev->cfgTimeout = cfgTimeout;
   dev->debug = cfgDebug;


   // Enforce that the FPGA advertises exactly one interrupt type. The cascade
   // below selects by *advertised capability*, not by what the firmware can
   // actually deliver: a bitstream that exposes both legacy INTx and MSI lets
   // pci_alloc_irq_vectors() prefer MSI, and if the user logic only drives the
   // legacy pin the driver ends up with interrupts enabled but none arriving
   // (silent DMA timeouts). Require a single type so the selection is
   // unambiguous, and abort loudly on a misconfigured core instead.
   {
      int     msiCap  = pci_find_capability(pcidev, PCI_CAP_ID_MSI);
      int     msixCap = pci_find_capability(pcidev, PCI_CAP_ID_MSIX);
      uint8_t intxPin = 0;
      int     irqTypes;

      pci_read_config_byte(pcidev, PCI_INTERRUPT_PIN, &intxPin);
      irqTypes = (intxPin != 0) + (msiCap != 0) + (msixCap != 0);

      if (irqTypes > 1) {
         dev_err(&pcidev->dev,
                 "%s: Probe: FPGA advertises multiple IRQ types "
                 "(INTx pin=%u, MSI=%s, MSI-X=%s); exactly one is required. "
                 "Rebuild the PCIe IP core with a single interrupt type.\n",
                 MOD_NAME, intxPin, msiCap ? "yes" : "no", msixCap ? "yes" : "no");
         probeReturn = -EINVAL;
         goto err_unmap;
      }
   }

   // Allocate IRQ vectors: try MSI-X first, then MSI, then legacy INTx.
   // pci_alloc_irq_vectors() walks the requested types in priority order
   // and returns the count of vectors negotiated; pci_irq_vector(pdev, 0)
   // returns the right Linux IRQ number regardless of which path won.
   ret = pci_alloc_irq_vectors(pcidev, 1, 1,
                               PCI_IRQ_MSIX | PCI_IRQ_MSI | PCI_IRQ_INTX);
   if (ret < 0) {
      dev_err(&pcidev->dev,
              "%s: Probe: pci_alloc_irq_vectors() = %i\n", MOD_NAME, ret);
      probeReturn = ret;
      goto err_unmap;
   }

   // pci_irq_vector() returns a signed errno on failure; capture it in an
   // int and reject < 0 before storing into the uint32_t dev->irq, otherwise
   // a negative value would wrap to a bogus IRQ and reach request_irq().
   ret = pci_irq_vector(pcidev, 0);
   if (ret < 0) {
      dev_err(&pcidev->dev,
              "%s: Probe: pci_irq_vector() = %i\n", MOD_NAME, ret);
      probeReturn = ret;
      goto err_unmap;
   }
   dev->irq = ret;
   if (pcidev->msix_enabled)      dev->irqType = DMA_IRQ_MSIX;
   else if (pcidev->msi_enabled)  dev->irqType = DMA_IRQ_MSI;
   else                           dev->irqType = DMA_IRQ_INTX;

   dev_info(dev->device,
            "Init: Probe: using %s interrupts, irq=%u\n",
            dev->irqType == DMA_IRQ_MSIX ? "MSI-X" :
            dev->irqType == DMA_IRQ_MSI  ? "MSI"   : "legacy INTx",
            dev->irq);

   // Set basic device context

   dev->hwFunc = hfunc;           // Hardware function pointer

   // Initialize device memory regions
   dev->reg    = dev->base + AGEN2_OFF;    // Register base address
   dev->rwBase = dev->base + PHY_OFF;      // Read/Write base address
   dev->rwSize = (2*USER_SIZE) - PHY_OFF;  // Read/Write region size

#ifdef DATA_GPU
   // GPU Init
   Gpu_Init(dev, GPU_ASYNC_CORE_OFFSET);
#endif

   // Manage device reset cycle
   dev_info(dev->device, "Init: Setting user reset\n");
   AxiVersion_SetUserReset(dev->base + AVER_OFF, true);  // Set user reset
   dev_info(dev->device, "Init: Clearing user reset\n");
   AxiVersion_SetUserReset(dev->base + AVER_OFF, false);  // Clear user reset

   // Configure DMA based on AXI address width: 128bit desc, = 64-bit address map
   if ((readl(dev->reg) & 0x10000) != 0) {
      axiWidth = (readl(dev->reg + 0x34) >> 8) & 0xFF;  // Extract AXI address width

      // Attempt to set DMA and coherent DMA masks based on AXI width
      if (!dma_set_mask(dev->device, DMA_BIT_MASK(axiWidth))) {
         dev_info(dev->device, "Init: Using %d-bit DMA mask.\n", axiWidth);

         if (!dma_set_coherent_mask(dev->device, DMA_BIT_MASK(axiWidth))) {
            dev_info(dev->device, "Init: Using %d-bit coherent DMA mask.\n", axiWidth);
         } else {
            dev_err(dev->device, "Init: Failed to set coherent DMA mask.\n");
            probeReturn = -EINVAL;
            goto err_unmap;
         }
      } else {
         dev_err(dev->device, "Init: Failed to set DMA mask.\n");
         probeReturn = -EINVAL;
         goto err_unmap;
      }
   }

   // Initialize common DMA functionalities. Dma_Init's early
   // failure paths (chrdev/class/device/proc) do not unmap the
   // registers we mapped above, so route through err_unmap.
   // Dma_UnmapReg is idempotent, so a double-unmap from a late
   // Dma_Init failure path is harmless. Preserve Dma_Init's
   // actual return code instead of forcing -ENOMEM, which
   // would mask the real failure reason.
   probeReturn = Dma_Init(dev);
   if (probeReturn < 0) {
      goto err_unmap;
   }

   // Log memory mapping information
   dev_info(dev->device, "Init: Reg space mapped to 0x%p.\n", dev->reg);
   dev_info(dev->device, "Init: User space mapped to 0x%p with size 0x%x.\n", dev->rwBase, dev->rwSize);
   dev_info(dev->device, "Init: Top Register = 0x%x\n", readl(dev->reg));

   // Finalize device probe successfully
   gDmaDevCount++;                   // Increment global device count
   probeReturn = 0;                  // Set successful return code
   return probeReturn;               // Return success

err_unmap:
#ifdef DATA_GPU
   // Gpu_Init may have allocated utilData before we got here.
   // kfree(NULL) is a no-op, so this stays safe even when
   // Gpu_Init never ran.
   kfree(dev->utilData);
   dev->utilData = NULL;
#endif
   Dma_UnmapReg(dev);               // Idempotent: safe even if Dma_MapReg never ran
   pci_free_irq_vectors(pcidev);    // Releases MSI/MSI-X/INTx allocation; no-op if
                                    // pci_alloc_irq_vectors failed or never ran
err_post_en:
   pci_disable_device(pcidev);      // Disable PCI device on failure
err_pre_en:
   memset(dev, 0, sizeof(*dev));    // Clear out the slot we took in gDmaDevices
   return probeReturn;
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

   // Skip devices with invalid BAR
   if (pci_resource_start(pcidev, 0) == 0) {
      pr_info("%s: Remove: Skipping device %04x:%02x:%02x.%d with invalid BAR0\n",
         MOD_NAME,
         pci_domain_nr(pcidev->bus),
         pcidev->bus->number,
         PCI_SLOT(pcidev->devfn),
         PCI_FUNC(pcidev->devfn));
      return;
   }

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

#ifdef DATA_GPU
   // Free GPU utility data allocated by Gpu_Init. gpu_async.c
   // does not own its teardown path; release here so unload
   // does not leak the kzalloc.
   if (dev->utilData != NULL) {
      kfree(dev->utilData);
      dev->utilData = NULL;
   }
#endif

   // Call common DMA clean function (calls free_irq() internally)
   Dma_Clean(dev);

   // Release MSI/MSI-X/INTx vectors (must follow free_irq, precede pci_disable_device)
   pci_free_irq_vectors(pcidev);

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
 * Return: the result of the command execution. Returns -EBADRQC (bad request number)
 * if the command is not recognized.
 */
int32_t DataDev_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg) {
   switch (cmd) {
      // GPU Commands
      // Handles adding or removing Nvidia memory based on the command specified.
      case GPU_Add_Nvidia_Memory:
      case GPU_Rem_Nvidia_Memory:
      case GPU_Set_Write_Enable:
      case GPU_Get_Gpu_Async_Ver:
      case GPU_Get_Max_Buffers:
#ifdef DATA_GPU
         return dev->gpuEn ? Gpu_Command(dev, cmd, arg) : -ENOTSUPP;
#else
         return -ENOTSUPP;
#endif
      case GPU_Is_Gpu_Async_Supp:
#ifdef DATA_GPU
         return dev->gpuEn ? 1 : 0;
#else
         return 0;
#endif
      case AVER_Get:
         // AXI Version Read
         return AxiVersion_Get(dev, dev->base + AVER_OFF, arg);
         break;

      default:
         // Delegate command to AxisG2 handler
         return AxisG2_Command(dev, cmd, arg);
         break;
   }
   return -EINVAL;
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
   struct pci_dev *pdev = dev->pcidev;

   // Show PCI Bus-Device-Function
   if (pdev) {
      seq_printf(s, "PCIe[BUS:NUM:SLOT.FUNC] : %04x:%02x:%02x.%x\n",
                  pci_domain_nr(pdev->bus),
                  pdev->bus->number,
                  PCI_SLOT(pdev->devfn),
                  PCI_FUNC(pdev->devfn));
   }

   // Read AXI version from device
   AxiVersion_Read(dev, dev->base + AVER_OFF, &aVer);

   // Display AXI version information
   AxiVersion_Show(s, dev, &aVer);

   // Display additional device-specific information
   AxisG2_SeqShow(s, dev);

#ifdef DATA_GPU
   if (dev->gpuEn) {
      // Display DataGPU-specific state information
      Gpu_Show(s, dev);
   }
#endif
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
   .irqEnable    = AxisG2_IrqEnable,
   .retRxBuffer  = AxisG2_RetRxBuffer,
   .sendBuffer   = AxisG2_SendBuffer,
   .command      = DataDev_Command,
   .seqShow      = DataDev_SeqShow,
};

// Parameters
module_param(cfgTxCount, int, 0);
MODULE_PARM_DESC(cfgTxCount, "TX buffer count");

module_param(cfgRxCount, int, 0);
MODULE_PARM_DESC(cfgRxCount, "RX buffer count");

module_param(cfgSize, int, 0);
MODULE_PARM_DESC(cfgSize, "Rx/TX Buffer size");

module_param(cfgMode, int, 0);
MODULE_PARM_DESC(cfgMode, "RX buffer mode");

module_param(cfgCont, int, 0);
MODULE_PARM_DESC(cfgCont, "RX continue enable");

module_param(cfgIrqHold, int, 0);
MODULE_PARM_DESC(cfgIrqHold, "IRQ Holdoff");

module_param(cfgIrqDis, int, 0);
MODULE_PARM_DESC(cfgIrqDis, "IRQ Disable");

module_param(cfgBgThold0, int, 0);
MODULE_PARM_DESC(cfgBgThold0, "Buff Group Threshold 0");

module_param(cfgBgThold1, int, 0);
MODULE_PARM_DESC(cfgBgThold1, "Buff Group Threshold 1");

module_param(cfgBgThold2, int, 0);
MODULE_PARM_DESC(cfgBgThold2, "Buff Group Threshold 2");

module_param(cfgBgThold3, int, 0);
MODULE_PARM_DESC(cfgBgThold3, "Buff Group Threshold 3");

module_param(cfgBgThold4, int, 0);
MODULE_PARM_DESC(cfgBgThold4, "Buff Group Threshold 4");

module_param(cfgBgThold5, int, 0);
MODULE_PARM_DESC(cfgBgThold5, "Buff Group Threshold 5");

module_param(cfgBgThold6, int, 0);
MODULE_PARM_DESC(cfgBgThold6, "Buff Group Threshold 6");

module_param(cfgBgThold7, int, 0);
MODULE_PARM_DESC(cfgBgThold7, "Buff Group Threshold 7");

module_param(cfgDevName, int, 0);
MODULE_PARM_DESC(cfgDevName, "Device Name Formating Setting");

module_param(cfgTimeout, int, 0);
MODULE_PARM_DESC(cfgTimeout, "Internal DMA transfer timeout duration (cycles)");

module_param(cfgDebug, int, 0);
MODULE_PARM_DESC(cfgDebug, "Enables very verbose debug logging. Use this with caution!");
