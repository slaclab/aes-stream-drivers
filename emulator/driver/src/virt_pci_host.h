/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    Virtual PCI host bridge interface for the FPGA emulator.
 *    Provides PCI configuration space emulation so the real datadev driver
 *    can probe a virtual device with vendor 0x1a4a / device 0x2030.
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

#ifndef __EMU_VIRT_PCI_HOST_H__
#define __EMU_VIRT_PCI_HOST_H__

#include <linux/pci.h>
#include "bar0_regs.h"

/* Must match data_dev_top.h */
#define EMU_PCI_VENDOR_ID    0x1a4a
#define EMU_PCI_DEVICE_ID    0x2030

/* PCI config space layout */
#define EMU_PCI_CFG_SIZE     256   /* Standard PCI config space */

/* Config space field offsets (standard PCI) */
#define EMU_CFG_VENDOR_ID    0x00
#define EMU_CFG_DEVICE_ID    0x02
#define EMU_CFG_COMMAND      0x04
#define EMU_CFG_STATUS       0x06
#define EMU_CFG_REVISION     0x08
#define EMU_CFG_CLASS_CODE   0x09
#define EMU_CFG_CACHE_LINE   0x0C
#define EMU_CFG_LATENCY      0x0D
#define EMU_CFG_HEADER_TYPE  0x0E
#define EMU_CFG_BAR0         0x10
#define EMU_CFG_BAR1         0x14
#define EMU_CFG_SUBSYS_VID   0x2C
#define EMU_CFG_SUBSYS_ID    0x2E
#define EMU_CFG_CAP_PTR      0x34
#define EMU_CFG_IRQ_LINE     0x3C
#define EMU_CFG_IRQ_PIN      0x3D

/**
 * struct emu_pci_host - Virtual PCI host bridge state
 * @bridge:   PCI host bridge allocated by pci_alloc_host_bridge()
 * @bus:      root PCI bus created during bus scan
 * @cfg_space: emulated PCI configuration space (256 bytes)
 * @bar:      pointer to BAR0 memory allocation state
 * @virq:     virtual IRQ number for the emulated device
 * @mem_res:  memory resource describing BAR0 region
 */
struct emu_pci_host {
   struct pci_host_bridge *bridge;
   struct pci_bus *bus;
   uint8_t cfg_space[EMU_PCI_CFG_SIZE];
   struct emu_bar0 *bar;         /* pointer to BAR0 data */
   unsigned int virq;            /* virtual IRQ number */
   struct resource mem_res;      /* memory resource for BAR0 */
};

/**
 * emu_pci_host_create - Create virtual PCI host bridge and scan bus
 * @host: host bridge state structure (caller-allocated)
 * @bar:  initialized BAR0 memory allocation
 * @virq: virtual IRQ number to assign to the emulated device
 *
 * Creates a PCI host bridge with custom pci_ops that emulate config space
 * for a device matching the datadev driver's PCI ID table. Scanning the
 * bus triggers PCI enumeration and driver probe.
 *
 * Return: 0 on success, negative errno on failure
 */
int emu_pci_host_create(struct emu_pci_host *host,
                        struct emu_bar0 *bar,
                        unsigned int virq);

/**
 * emu_pci_host_destroy - Tear down the virtual PCI host bridge
 * @host: host bridge state to destroy
 *
 * Stops and removes the PCI root bus and cleans up all resources.
 */
void emu_pci_host_destroy(struct emu_pci_host *host);

/**
 * emu_pci_notifier_init - Register a PCI bus notifier to re-clear per-device
 *                         CMA state on every driver bind.
 * @host: emu_pci_host whose bus should be the filter target
 *
 * Registers a bus notifier on pci_bus_type. On BUS_NOTIFY_BIND_DRIVER for
 * any pci_dev on @host->bus, re-zeroes pdev->dev.cma_area. This prevents
 * a stale/garbage cma_area pointer from being dereferenced on datadev
 * rebind after a prior BUFF_STREAM insmod cycle.
 *
 * Return: 0 on success, negative errno on failure
 */
int emu_pci_notifier_init(struct emu_pci_host *host);

/**
 * emu_pci_notifier_destroy - Unregister the PCI bus notifier.
 * @host: emu_pci_host used at init time
 *
 * Idempotent (safe to call if init failed or was never called).
 */
void emu_pci_notifier_destroy(struct emu_pci_host *host);

#endif  /* __EMU_VIRT_PCI_HOST_H__ */
