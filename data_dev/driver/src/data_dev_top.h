/**
 *-----------------------------------------------------------------------------
 * Title      : Top level module
 * ----------------------------------------------------------------------------
 * File       : data_dev_top.h
 * Created    : 2017-03-17
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
#ifndef __DATA_DEV_TOP_H__
#define __DATA_DEV_TOP_H__

#include <linux/types.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <dma_common.h>

// PCI ID
#define PCI_VENDOR_ID_SLAC 0x1a4a
#define PCI_DEVICE_ID_DDEV 0x2020

// Init Kernel Module
int32_t DataDev_Init(void);

// Exit Kernel Module
void DataDev_Exit(void);

// Create and init device
int DataDev_Probe(struct pci_dev *pcidev, const struct pci_device_id *dev_id);

// Cleanup device
void  DataDev_Remove(struct pci_dev *pcidev);

// Execute command
int32_t DataDev_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);

// Add data to proc dump
void DataDev_SeqShow(struct seq_file *s, struct DmaDevice *dev);

// Set functions for gen2 card
extern struct hardware_functions DataDev_functions;

#endif

