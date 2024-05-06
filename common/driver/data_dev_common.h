/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    This header file defines common driver operations for data_dev and
 *    data_gpu.
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
#ifndef __DRIVER_COMMON_H__
#define __DRIVER_COMMON_H__

#include <linux/types.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <dma_common.h>

/** Maximum number of DMA devices supported. */
#define MAX_DMA_DEVICES 32

/** PCI vendor and device identifiers. */
#define PCI_VENDOR_ID_SLAC 0x1a4a
#define PCI_DEVICE_ID_DDEV 0x2030

/** Address map for device registers. */
#define AGEN2_OFF   0x00000000 /**< DMAv2 Engine Offset */
#define AGEN2_SIZE  0x00010000 /**< DMAv2 Engine Size */
#define PHY_OFF     0x00010000 /**< PCIe PHY Offset */
#define PHY_SIZE    0x00010000 /**< PCIe PHY Size */
#define AVER_OFF    0x00020000 /**< AxiVersion Offset */
#define AVER_SIZE   0x00010000 /**< AxiVersion Size */
#define PROM_OFF    0x00030000 /**< PROM Offset */
#define PROM_SIZE   0x00050000 /**< PROM Size */
#define USER_OFF    0x00800000 /**< User Space Offset */
#define USER_SIZE   0x00800000 /**< User Space Size */

/** Globals */
extern struct DmaDevice gDmaDevices[MAX_DMA_DEVICES];
extern int cfgMode;
extern int cfgDevName;

/** Globals (these must be defined by each driver) */
extern const char* gModName;
extern struct pci_driver* gPciDriver;
extern struct hardware_functions* gHardwareFuncs;

/** Types */
typedef int(*Probe_Init_Cfg)(struct DmaDevice* dev);    /** Return 0 for success, <0 for error */

/** Function prototypes */
extern int32_t DataDev_Common_Init(void);
extern void DataDev_Common_Exit(void);
extern void DataDev_Common_Remove(struct pci_dev *pcidev);
extern void DataDev_Common_SeqShow(struct seq_file *s, struct DmaDevice *dev);
extern int32_t DataDev_Common_Command(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);
extern int DataDev_Common_Probe(struct pci_dev *pcidev, const struct pci_device_id *dev_id, Probe_Init_Cfg initCfg);

#endif // __DRIVER_COMMON_H__