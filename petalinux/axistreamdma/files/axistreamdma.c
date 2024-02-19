/**
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
#include <rce_top.h>
#include <dma_common.h>
#include <dma_buffer.h>
#include <axis_gen1.h>
#include <axis_gen2.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

// Init Configuration values
int cfgTxCount0 = 128;
int cfgTxCount1 = 8;
int cfgTxCount2 = 8;
int cfgRxCount0 = 128;
int cfgRxCount1 = 8;
int cfgRxCount2 = 8;
int cfgSize0    = 2097152;
int cfgSize1    = 4096;
int cfgSize2    = 4096;
int cfgMode0    = BUFF_COHERENT;
int cfgMode1    = BUFF_COHERENT;
int cfgMode2    = BUFF_ARM_ACP | AXIS2_RING_ACP;

struct DmaDevice gDmaDevices[MAX_DMA_DEVICES];

// Tables of device names
const char * RceDevNames[MAX_DMA_DEVICES] = {
   "axi_stream_dma_0",
   "axi_stream_dma_1",
   "axi_stream_dma_2",
   "axi_stream_dma_3",
};

// Module Name
#define MOD_NAME "axi_stream_dma"

MODULE_AUTHOR("Ryan Herbst");
MODULE_DESCRIPTION("AXI Stream DMA driver. V3");
MODULE_LICENSE("GPL");

/**
 * Rce_runtime_suspend - Suspend routine for runtime power management.
 *
 * This function is intended to be invoked by the runtime power management
 * framework when the device is being suspended. It should encapsulate any
 * hardware-specific logic required to safely bring the device into a low-power
 * state. Currently, this function does not implement device-specific actions
 * and simply returns success.
 *
 * @dev: Device structure representing the DMA device.
 * @return: Always returns 0, indicating success.
 */
static int Rce_runtime_suspend(struct device *dev)
{
   // Currently, there are no device-specific suspend actions required.
   // Placeholder for implementing suspend logic specific to the device.
   return 0;
}

/**
 * Rce_runtime_resume - Resume routine for runtime power management.
 *
 * This function is designed to be called by the runtime power management
 * framework when the device is transitioning back to its active state from
 * suspension. It should encompass any hardware-specific logic needed to
 * reinitialize the device and restore its operational state. Currently, this
 * function does not perform any device-specific actions and merely returns
 * success, serving as a placeholder for future implementation.
 *
 * @dev: Device structure representing the DMA device.
 * @return: Always returns 0, indicating success.
 */
static int Rce_runtime_resume(struct device *dev)
{
   // Currently, no device-specific resume actions are required.
   // Placeholder for future implementation of resume logic specific to the device.
   return 0;
}

/**
 * Rce_Dma_pm_ops - Power management operations structure.
 *
 * This structure defines the runtime power management callbacks for the DMA
 * device. It specifies the functions to be called during suspend and resume
 * phases of the runtime power management. The NULL argument for the idle
 * function indicates that no specific action is required during the idle phase.
 *
 * The use of SET_RUNTIME_PM_OPS macro simplifies the assignment of the
 * suspend, resume, and idle callbacks within the device's power management
 * operations structure.
 */
static const struct dev_pm_ops Rce_Dma_pm_ops = {
   .runtime_suspend = Rce_runtime_suspend,
   .runtime_resume  = Rce_runtime_resume,
   .runtime_idle    = NULL
};


static struct of_device_id Rce_DmaMatch[] = {
   { .compatible = MOD_NAME, },
   { /* This is filled with module_parm */ },
   { /* Sentinel */ },
};

static struct platform_driver Rce_DmaPdrv = {
   .probe  = Rce_Probe,
   .remove = Rce_Remove,
   .driver = {
      .name = MOD_NAME,
      .owner = THIS_MODULE,
      .pm = &Rce_Dma_pm_ops,
      .of_match_table = of_match_ptr(Rce_DmaMatch),
   },
};

module_platform_driver(Rce_DmaPdrv);

/**
 * Rce_Probe - Initializes the DMA device upon detection.
 *
 * This function gets called by the Linux kernel upon finding a platform device
 * that matches the driver's device ID table. It handles the device's initialization
 * by allocating necessary resources, registering the device, and setting up DMA
 * configurations. It locates the appropriate device instance based on its name,
 * initializes the device structure, maps device memory regions, and sets up DMA
 * parameters accordingly.
 *
 * @pdev: The platform device structure representing the DMA device.
 * @return: 0 on success, or a negative error code on failure.
 */
int Rce_Probe(struct platform_device *pdev) {
   struct DmaDevice *dev;
   int32_t x;
   const char *tmpName;
   int32_t tmpIdx;

   // Extract device name from platform device structure
   tmpName = pdev->name + 9;

   // Search for a matching device name in the predefined device names list
   tmpIdx = -1;
   for (x = 0; x < MAX_DMA_DEVICES; x++) {
      if (strcmp(tmpName, RceDevNames[x]) == 0) {
         tmpIdx = x;
         break;
      }
   }

   // Log warning and exit if no matching device is found
   if (tmpIdx < 0) {
      pr_warn("%s: Probe: Matching device not found: %s.\n", MOD_NAME, tmpName);
      return -1;
   }

   // Select the device structure for the found device
   dev = &gDmaDevices[tmpIdx];
   pr_info("%s: Probe: Using index %i for %s.\n", MOD_NAME, tmpIdx, tmpName);

   // Initialize the device structure to zeros
   memset(dev, 0, sizeof(struct DmaDevice));
   dev->index = tmpIdx;

   // Copy the device name to the device structure
   if (strscpy(dev->devName, tmpName, sizeof(dev->devName)) < 0) {
      pr_err("%s: Probe: Source string too long for destination: %s.\n", MOD_NAME, tmpName);
      return -1;
   }

   // Retrieve the base address and size of the device's memory from the platform device
   dev->baseAddr = pdev->resource[0].start;
   dev->baseSize = pdev->resource[0].end - pdev->resource[0].start + 1;

   // Obtain the device's IRQ number from the platform device
   dev->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);

   // Set additional device fields
   dev->device = &(pdev->dev);

   // Map device memory to enable probing
   if (Dma_MapReg(dev) < 0) return -1;

   // Configure device settings based on the selected index
   switch (tmpIdx) {
      case 0:
         dev->cfgTxCount = cfgTxCount0;
         dev->cfgRxCount = cfgRxCount0;
         dev->cfgSize = cfgSize0;
         dev->cfgMode = cfgMode0;
         break;
      case 1:
         dev->cfgTxCount = cfgTxCount1;
         dev->cfgRxCount = cfgRxCount1;
         dev->cfgSize = cfgSize1;
         dev->cfgMode = cfgMode1;
         break;
      case 2:
         dev->cfgTxCount = cfgTxCount2;
         dev->cfgRxCount = cfgRxCount2;
         dev->cfgSize = cfgSize2;
         dev->cfgMode = cfgMode2;
         break;
      default:
         return -1; // Invalid index
   }

   // Instance-independent configuration
   dev->cfgCont = 1;

   // Determine hardware functions based on the device version
   if (((readl(dev->reg) >> 24) & 0xFF) >= 2) {
      dev->hwFunc = &(AxisG2_functions);
   } else {
      writel(0x1, ((uint8_t *)dev->reg) + 0x8);
      if (readl(((uint8_t *)dev->reg) + 0x8) != 0x1) {
         pr_info("%s: Probe: Empty register space. Exiting.\n", MOD_NAME);
         return -1;
      }
      dev->hwFunc = &(AxisG1_functions);
   }

   // Check for coherent DMA mode and set if not on arm64
#if !defined(__aarch64__)
   if ((dev->cfgMode & BUFF_ARM_ACP) || (dev->cfgMode & AXIS2_RING_ACP)) {
      // Set DMA operations to coherent if supported
      set_dma_ops(&pdev->dev, &arm_coherent_dma_ops);
      pr_info("%s: Probe: Set COHERENT DMA =%i\n", dev->device, dev->cfgMode);
   }
#endif

   // Initialize DMA and check for success
   if (Dma_Init(dev) < 0)
      return -1; // Return error if DMA initialization fails

   // Successful DMA initialization increments device count
   gDmaDevCount++;

   // Enable runtime power management for the device
   pm_runtime_enable(&pdev->dev);

   // Return success
   return 0;
}

/**
 * Rce_Remove - Clean up resources upon removal of the DMA device.
 *
 * This function is invoked when a DMA device is detached from the system.
 * It performs necessary cleanup operations such as disabling the device's
 * power management, decrementing the global device count, and invoking
 * the common DMA cleanup function. Additionally, it logs the removal
 * process for debugging and auditing purposes.
 *
 * @pdev: Platform device structure representing the DMA device.
 * @return: Returns 0 on successful cleanup, -1 if no matching device is found.
 */
int Rce_Remove(struct platform_device *pdev)
{
   int32_t x;
   const char *tmpName;
   int32_t tmpIdx;
   struct DmaDevice *dev = NULL;

   pr_info("%s: Remove: Removal process initiated.\n", MOD_NAME);

   // Disable runtime power management before removing the device
   pm_runtime_disable(&pdev->dev);

   // Extract the device name suffix for identification
   tmpName = pdev->name + 9;

   // Search for the matching device entry in the global device list
   tmpIdx = -1;
   for (x = 0; x < MAX_DMA_DEVICES; x++) {
      if (strcmp(tmpName, RceDevNames[x]) == 0) {
         tmpIdx = x;
         break;
      }
   }

   // Exit if no matching device is found
   if (tmpIdx < 0) {
      pr_info("%s: Remove: No matching device found.\n", MOD_NAME);
      return -1;
   }

   // Retrieve the device structure and update the global device count
   dev = &gDmaDevices[tmpIdx];
   gDmaDevCount--;

   // Invoke common cleanup operations for the DMA device
   Dma_Clean(dev);

   pr_info("%s: Remove: Device removal completed.\n", MOD_NAME);
   return 0;
}

// Parameters
module_param(cfgTxCount0,int,0);
MODULE_PARM_DESC(cfgTxCount0, "TX buffer count");

module_param(cfgTxCount1,int,0);
MODULE_PARM_DESC(cfgTxCount1, "TX buffer count");

module_param(cfgTxCount2,int,0);
MODULE_PARM_DESC(cfgTxCount2, "TX buffer count");

module_param(cfgRxCount0,int,0);
MODULE_PARM_DESC(cfgRxCount0, "RX buffer count");

module_param(cfgRxCount1,int,0);
MODULE_PARM_DESC(cfgRxCount1, "RX buffer count");

module_param(cfgRxCount2,int,0);
MODULE_PARM_DESC(cfgRxCount2, "RX buffer count");

module_param(cfgSize0,int,0);
MODULE_PARM_DESC(cfgSize0, "RX/TX buffer size");

module_param(cfgSize1,int,0);
MODULE_PARM_DESC(cfgSize1, "RX/TX buffer size");

module_param(cfgSize2,int,0);
MODULE_PARM_DESC(cfgSize2, "RX/TX buffer size");

module_param(cfgMode0,int,0);
MODULE_PARM_DESC(cfgMode0, "RX buffer mode");

module_param(cfgMode1,int,0);
MODULE_PARM_DESC(cfgMode1, "RX buffer mode");

module_param(cfgMode2,int,0);
MODULE_PARM_DESC(cfgMode2, "RX buffer mode");
