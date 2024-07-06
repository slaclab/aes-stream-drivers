/**
 *-----------------------------------------------------------------------------
 * Title      : Top level module
 * ----------------------------------------------------------------------------
 * File       : rce_top.c
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
#include <linux/dma-map-ops.h>

// Init Configuration values
int cfgTxCount0 = 8;
int cfgTxCount1 = 8;
int cfgTxCount2 = 8;
int cfgRxCount0 = 8;
int cfgRxCount1 = 8;
int cfgRxCount2 = 800;
int cfgSize0    = 4096*4;
int cfgSize1    = 4096;
int cfgSize2    = 4096*4;
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
      if (strcmp(tmpName,RceDevNames[x]) == 0) {
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
   strcpy(dev->devName,tmpName);//NOLINT

   // Get Base Address of registers from pci structure.
   dev->baseAddr = pdev->resource[0].start;
   dev->baseSize = pdev->resource[0].end - pdev->resource[0].start + 1;

   // Get IRQ from pci_dev structure.
   dev->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);;

   // Set device fields
   dev->device = &(pdev->dev);

   // Map memory now in order to probe
   if ( Dma_MapReg(dev) < 0 ) return(-1);

   // Determine which index to use
   switch (tmpIdx) {
      case 0:
         dev->cfgTxCount = cfgTxCount0;
         dev->cfgRxCount = cfgRxCount0;
         dev->cfgSize    = cfgSize0;
         dev->cfgMode    = cfgMode0;
         break;
      case 1:
         dev->cfgTxCount = cfgTxCount1;
         dev->cfgRxCount = cfgRxCount1;
         dev->cfgSize    = cfgSize1;
         dev->cfgMode    = cfgMode1;
         break;
      case 2:
         dev->cfgTxCount = cfgTxCount2;
         dev->cfgRxCount = cfgRxCount2;
         dev->cfgSize    = cfgSize2;
         dev->cfgMode    = cfgMode2;
         break;
      default:
         return(-1);
         break;
   }

   // Instance independent
   dev->cfgCont = 1;

   // Set hardware functions
   // Version 2
   if ( ((ioread32(dev->reg) >> 24) & 0xFF) >= 2 ) {
      dev->hwFunc = &(AxisG2_functions);

   // Version 1
   } else {
      iowrite32(0x1,((uint8_t *)dev->reg)+0x8);
      if ( ioread32(((uint8_t *)dev->reg)+0x8) != 0x1 ) {
         release_mem_region(dev->baseAddr, dev->baseSize);
         dev_info(dev->device,"Probe: Empty register space. Exiting\n");
         return(-1);
      }
      dev->hwFunc = &(AxisG1_functions);
   }

   // Coherent
   /* not available on arm64 */
#if ! defined( __aarch64__)
   if( (dev->cfgMode & BUFF_ARM_ACP) || (dev->cfgMode & AXIS2_RING_ACP) ) {
       set_dma_ops(&pdev->dev,&arm_coherent_dma_ops);
       dev_info(dev->device,"Probe: Set COHERENT DMA =%i\n",dev->cfgMode);
   }
#endif
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
      if (strcmp(tmpName,RceDevNames[x]) == 0) {
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

