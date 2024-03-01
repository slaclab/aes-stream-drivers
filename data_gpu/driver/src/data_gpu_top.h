/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    Defines the top-level module types and functions for the Data GPU Device driver.
 *    This driver is part of the aes_stream_drivers package.
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

#ifndef __DATA_GPU_TOP_H__
#define __DATA_GPU_TOP_H__

#include <linux/types.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <dma_common.h>

/* Maximum number of DMA devices */
#define MAX_DMA_DEVICES 4

/* PCI IDs */
#define PCI_VENDOR_ID_SLAC 0x1a4a
#define PCI_DEVICE_ID_DDEV 0x2030

/* Memory mapping offsets and sizes */
#define AGEN2_OFF   0x00000000
#define AGEN2_SIZE  0x00010000
#define PHY_OFF     0x00010000
#define PHY_SIZE    0x00010000
#define AVER_OFF    0x00020000
#define AVER_SIZE   0x00010000
#define PROM_OFF    0x00030000
#define PROM_SIZE   0x00050000
#define USER_OFF    0x00800000
#define USER_SIZE   0x00800000
#define GPU_OFF     0x00A00000

/* Function prototypes */
int32_t DataGpu_Init(void);
void DataGpu_Exit(void);
int DataGpu_Probe(struct pci_dev *pcidev, const struct pci_device_id *dev_id);
void DataGpu_Remove(struct pci_dev *pcidev);
int32_t DataGpu_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);
void DataGpu_SeqShow(struct seq_file *s, struct DmaDevice *dev);

/* Hardware function operations */
extern struct hardware_functions DataGpu_functions;

#endif /* __DATA_GPU_TOP_H__ */
