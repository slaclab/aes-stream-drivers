/**
 *-----------------------------------------------------------------------------
 * Title      : Top level module
 * ----------------------------------------------------------------------------
 * File       : axis_top.c
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
#include "axis_top.h"
#include "dma_common.h"
#include "axis_gen1.h"
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
int cfgTxCount[4] = {8,8,8,0};
int cfgRxCount[4] = {8,8,1000,0};
int cfgSize[4]    = {4096*4,4096,4096*4,4096};
int cfgRxMode[4]  = {1,1,3,1}; // BUFF_COHERENT for 0,1,3, BUFF_ARM_ACP for 2
int cfgTxMode[4]  = {1,1,3,1}; // BUFF_COHERENT for 0,1,3, BUFF_ARM_ACP for 2

// Tables of device names
const char * AxisDevNames[MAX_DMA_DEVICES] = {
   "axi_stream_dma_0",
   "axi_stream_dma_1",
   "axi_stream_dma_2",
   "axi_stream_dma_3"
};

// Module Name
#define MOD_NAME "axi_stream_dma"

MODULE_AUTHOR("Ryan Herbst");
MODULE_DESCRIPTION("AXI Stream DMA driver. V3");
MODULE_LICENSE("GPL");

static int Axis_DmaNop(struct device *dev) {
   return 0;
}

static const struct dev_pm_ops Axis_DmaOps = {
   .runtime_suspend = Axis_DmaNop,
   .runtime_resume = Axis_DmaNop,
};

static struct of_device_id Axis_DmaMatch[] = {
   { .compatible = MOD_NAME, },
   { /* This is filled with module_parm */ },
   { /* Sentinel */ },
};

static struct platform_driver Axis_DmaPdrv = {
   .probe  = Axis_Probe,
   .remove = Axis_Remove,
   .driver = {
      .name = MOD_NAME,
      .owner = THIS_MODULE,
      .pm = &Axis_DmaOps,
      .of_match_table = of_match_ptr(Axis_DmaMatch),
   },
};

module_platform_driver(Axis_DmaPdrv);

// Create and init device
int Axis_Probe(struct platform_device *pdev) {
   struct DmaDevice *dev;
   struct hardware_functions *hfunc;

   int32_t      x;
   const char * tmpName;
   int32_t      tmpIdx;

   // Set hardware functions
   hfunc = &(AxisG1_functions);

   // Extract name
   tmpName = pdev->name + 9;

   // Find matching entry
   tmpIdx = -1;
   for ( x=0; x < MAX_DMA_DEVICES; x++ ) {
      if (strcmp(tmpName,AxisDevNames[x]) == 0) {
         tmpIdx = x;
         break;
      }
   }

   // Matching device not found
   if ( tmpIdx < 0 ) {
      pr_warn("%s: Probe: Matching device not found: %s.\n", MOD_NAME,tmpName);
      return(-1);
   }
   dev = &gDmaDevices[tmpIdx];

   pr_info("%s: Probe: Using index %i for %s.\n", MOD_NAME, tmpIdx, tmpName);

   // Init structure
   memset(dev,0,sizeof(struct DmaDevice));
   dev->index = tmpIdx;

   // Increment count
   gDmaDevCount++;

   // Create a device name
   strcpy(dev->devName,tmpName);

   // Get Base Address of registers from pci structure.
   dev->baseAddr = pdev->resource[0].start;
   dev->baseSize = pdev->resource[0].end - pdev->resource[0].start + 1;

   // Set configuration
   dev->cfgTxCount = cfgTxCount[tmpIdx];
   dev->cfgRxCount = cfgRxCount[tmpIdx];
   dev->cfgSize    = cfgSize[tmpIdx];
   dev->cfgRxMode  = cfgRxMode[tmpIdx];
   dev->cfgTxMode  = cfgTxMode[tmpIdx];

   // Get IRQ from pci_dev structure. 
   dev->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);;

   // Set device fields
   dev->device       = &(pdev->dev);
   dev->hwFunctions  = hfunc;

   /* Coherent DMA set via module parameter or device tree entry. */
   if(dev->cfgRxMode == BUFF_ARM_ACP || dev->cfgTxMode == BUFF_ARM_ACP) {
       set_dma_ops(&pdev->dev,&arm_coherent_dma_ops);
       dev_info(dev->device,"Probe: Set COHERENT DMA ops\n");
   }

   // Call common dma init function
   return(Dma_Init(dev));
}


// Cleanup device
int Axis_Remove(struct platform_device *pdev) {
   int32_t      x;
   const char * tmpName;
   int32_t      tmpIdx;

   struct DmaDevice *dev = NULL;

   pr_info("%s: Remove: Remove called.\n", MOD_NAME);

   tmpName = pdev->name + 9;

   // Find matching entry
   tmpIdx = -1;
   for ( x=0; x < MAX_DMA_DEVICES; x++ ) {
      if (strcmp(tmpName,AxisDevNames[x]) == 0) {
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
module_param_array(cfgTxCount,int,0,0);
MODULE_PARM_DESC(cfgTxCount, "TX buffer count");

module_param_array(cfgRxCount,int,0,0);
MODULE_PARM_DESC(cfgRxCount, "RX buffer count");

module_param_array(cfgSize,int,0,0);
MODULE_PARM_DESC(cfgSize, "RX/TX buffer size");

module_param_array(cfgRxMode,int,0,0);
MODULE_PARM_DESC(cfgRxMode, "RX buffer mode");

module_param_array(cfgTxMode,int,0,0);
MODULE_PARM_DESC(cfgTxMode, "TX buffer mode");

