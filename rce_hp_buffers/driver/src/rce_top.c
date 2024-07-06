/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
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
#include <rce_top.h>
#include <dma_common.h>
#include <dma_buffer.h>
#include <rce_hp.h>
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
int cfgCount = 1000;
int cfgSize  = 4096*4;

struct DmaDevice gDmaDevices[MAX_DMA_DEVICES];

// Tables of device names
const char * RceDevNames[MAX_DMA_DEVICES] = { "rce_hp_0" };

// Module Name
#define MOD_NAME "rce_hp"

MODULE_AUTHOR("Ryan Herbst");
MODULE_DESCRIPTION("AXI Stream DMA driver. V3");
MODULE_LICENSE("GPL");

static int Rce_DmaNop(struct device *dev) {
   return 0;
}

static const struct dev_pm_ops Rce_DmaOps = {
   .runtime_suspend = Rce_DmaNop,
   .runtime_resume = Rce_DmaNop,
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
      .pm = &Rce_DmaOps,
      .of_match_table = of_match_ptr(Rce_DmaMatch),
   },
};

module_platform_driver(Rce_DmaPdrv);

// Create and init device
int Rce_Probe(struct platform_device *pdev) {
   struct DmaDevice *dev;

   int32_t      x;
   const char * tmpName;
   int32_t      tmpIdx;

   // Extract name
   tmpName = pdev->name + 9;

   // Find matching entry
   tmpIdx = -1;
   for ( x=0; x < MAX_DMA_DEVICES; x++ ) {
      if (strcmp(tmpName, RceDevNames[x]) == 0) {
         tmpIdx = x;
         break;
      }
   }

   // Matching device not found
   if ( tmpIdx < 0 ) {
      pr_warn("%s: Probe: Matching device not found: %s.\n", MOD_NAME, tmpName);
      return(-1);
   }
   dev = &gDmaDevices[tmpIdx];

   pr_info("%s: Probe: Using index %i for %s.\n", MOD_NAME, tmpIdx, tmpName);

   // Init structure
   memset(dev, 0, sizeof(struct DmaDevice));
   dev->index = tmpIdx;

   // Increment count
   gDmaDevCount++;

   // Create a device name
   strcpy(dev->devName,tmpName);//NOLINT

   // Get Base Address of registers from pci structure.
   dev->baseAddr = pdev->resource[0].start;
   dev->baseSize = pdev->resource[0].end - pdev->resource[0].start + 1;

   // No IRQ
   dev->irq = 0;

   // Set device fields
   dev->device = &(pdev->dev);

   // Setup config
   dev->cfgTxCount = 0;
   dev->cfgRxCount = cfgCount;
   dev->cfgSize    = cfgSize;
   dev->cfgMode    = BUFF_COHERENT;

   // Set hardware functions
   dev->hwFunc = &(RceHp_functions);

   // Call common dma init function
   return(Dma_Init(dev));
}


// Cleanup device
int Rce_Remove(struct platform_device *pdev) {
   int32_t      x;
   const char * tmpName;
   int32_t      tmpIdx;

   struct DmaDevice *dev = NULL;

   pr_info("%s: Remove: Remove called.\n", MOD_NAME);

   tmpName = pdev->name + 9;

   // Find matching entry
   tmpIdx = -1;
   for ( x=0; x < MAX_DMA_DEVICES; x++ ) {
      if (strcmp(tmpName, RceDevNames[x]) == 0) {
         tmpIdx = x;
         break;
      }
   }

   // Matching device not found
   if ( tmpIdx < 0 ) {
      pr_info("%s: Remove: Matching device not found.\n", MOD_NAME);
      return(-1);
   }
   dev = &gDmaDevices[tmpIdx];

   // Decrement count
   gDmaDevCount--;

   // Call common dma init function
   Dma_Clean(dev);
   pr_info("%s: Remove: Driver is unloaded.\n", MOD_NAME);
   return(0);
}

// Parameters
module_param(cfgCount, int, 0);
MODULE_PARM_DESC(cfgCount, "Buffer count");

module_param(cfgSize, int, 0);
MODULE_PARM_DESC(cfgSize, "Buffer size");

