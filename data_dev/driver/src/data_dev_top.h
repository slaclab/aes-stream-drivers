/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    Defines the top-level module types and functions for the Data Device driver.
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

#ifndef __DATA_DEV_TOP_H__
#define __DATA_DEV_TOP_H__

#include <linux/types.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <dma_common.h>

// Function prototypes
int32_t DataDev_Init(void);
void DataDev_Exit(void);
int DataDev_Probe(struct pci_dev *pcidev, const struct pci_device_id *dev_id);
void  DataDev_Remove(struct pci_dev *pcidev);
int32_t DataDev_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);
void DataDev_SeqShow(struct seq_file *s, struct DmaDevice *dev);
extern struct hardware_functions DataDev_functions;

#endif // __DATA_DEV_TOP_H__
