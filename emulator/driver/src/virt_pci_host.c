/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    Virtual PCI host bridge and configuration space emulation.
 *    Creates a PCI host bridge with custom pci_ops so the PCI subsystem
 *    discovers a virtual device with vendor 0x1a4a / device 0x2030.
 *    The real datadev driver's PCI ID table matches this device and
 *    triggers DataDev_Probe() without modification.
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

#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>
#include <linux/notifier.h>
#include <linux/device/bus.h>

#include "virt_pci_host.h"

/* File-static pointer for the bus notifier to filter devices by bus.
 * Set by emu_pci_notifier_init(), cleared by emu_pci_notifier_destroy(). */
static struct emu_pci_host *g_emu_host;

/* Helper: write a 16-bit value into config space byte array (little-endian) */
static void cfg_write16(uint8_t *cfg, int offset, uint16_t val)
{
   cfg[offset]     = val & 0xFF;
   cfg[offset + 1] = (val >> 8) & 0xFF;
}

/* Helper: write a 32-bit value into config space byte array (little-endian) */
static void cfg_write32(uint8_t *cfg, int offset, uint32_t val)
{
   cfg[offset]     = val & 0xFF;
   cfg[offset + 1] = (val >> 8) & 0xFF;
   cfg[offset + 2] = (val >> 16) & 0xFF;
   cfg[offset + 3] = (val >> 24) & 0xFF;
}

/**
 * emu_pci_init_cfg_space - Populate PCI config space with static values
 * @host: host bridge state with cfg_space, bar, and virq set
 *
 * Fills in standard PCI config space fields so the PCI subsystem and
 * the datadev driver see a valid device during bus scan.
 */
static void emu_pci_init_cfg_space(struct emu_pci_host *host)
{
   uint8_t *cfg = host->cfg_space;
   uint32_t bar0_val;

   /* Zero the entire config space first */
   memset(cfg, 0, EMU_PCI_CFG_SIZE);

   /* Vendor ID = 0x1a4a (SLAC) */
   cfg_write16(cfg, EMU_CFG_VENDOR_ID, EMU_PCI_VENDOR_ID);

   /* Device ID = 0x2030 (DataDev) */
   cfg_write16(cfg, EMU_CFG_DEVICE_ID, EMU_PCI_DEVICE_ID);

   /* Command = 0x0000 (driver sets bus master + mem space enable) */
   cfg_write16(cfg, EMU_CFG_COMMAND, 0x0000);

   /* Status = 0x0010 (capabilities list present) */
   cfg_write16(cfg, EMU_CFG_STATUS, 0x0010);

   /* Revision = 0x01 */
   cfg[EMU_CFG_REVISION] = 0x01;

   /* Class code: Network controller, Other subclass
    * [0x09] = prog_if = 0x00
    * [0x0A] = subclass = 0x80
    * [0x0B] = class    = 0x02
    */
   cfg[EMU_CFG_CLASS_CODE]     = 0x00;  /* prog_if */
   cfg[EMU_CFG_CLASS_CODE + 1] = 0x80;  /* subclass */
   cfg[EMU_CFG_CLASS_CODE + 2] = 0x02;  /* class */

   /* Header type = 0x00 (standard) */
   cfg[EMU_CFG_HEADER_TYPE] = 0x00;

   /*
    * BAR0: 64-bit memory BAR (bit 2 = 1 for 64-bit, bit 3 = 1 for prefetchable)
    * Low 32 bits contain physical address with type bits in low nibble
    */
   bar0_val = (uint32_t)(host->bar->phys & 0xFFFFFFF0ULL) | 0x04;
   cfg_write32(cfg, EMU_CFG_BAR0, bar0_val);

   /* BAR1: upper 32 bits of 64-bit BAR0 address */
   cfg_write32(cfg, EMU_CFG_BAR1,
               (uint32_t)((host->bar->phys >> 32) & 0xFFFFFFFF));

   /* Subsystem Vendor ID = 0x1a4a */
   cfg_write16(cfg, EMU_CFG_SUBSYS_VID, EMU_PCI_VENDOR_ID);

   /* Subsystem Device ID = 0x2030 */
   cfg_write16(cfg, EMU_CFG_SUBSYS_ID, EMU_PCI_DEVICE_ID);

   /* IRQ line = virtual IRQ number */
   cfg[EMU_CFG_IRQ_LINE] = (uint8_t)(host->virq & 0xFF);

   /* IRQ pin = INTA# */
   cfg[EMU_CFG_IRQ_PIN] = 0x01;
}

/**
 * emu_pci_read - PCI config space read callback
 * @bus:   PCI bus being read
 * @devfn: device/function number (only devfn 0 has our device)
 * @where: config space offset
 * @size:  read size in bytes (1, 2, or 4)
 * @val:   output value
 *
 * Return: PCIBIOS_SUCCESSFUL, PCIBIOS_DEVICE_NOT_FOUND, or
 *         PCIBIOS_BAD_REGISTER_NUMBER
 */
static int emu_pci_read(struct pci_bus *bus, unsigned int devfn,
                        int where, int size, u32 *val)
{
   struct emu_pci_host *host = bus->sysdata;
   uint8_t *cfg = host->cfg_space;

   /* Only respond to device 0, function 0 */
   if (devfn != 0) {
      *val = 0xFFFFFFFF;
      return PCIBIOS_DEVICE_NOT_FOUND;
   }

   /* Bounds check */
   if (where + size > EMU_PCI_CFG_SIZE) {
      *val = 0xFFFFFFFF;
      return PCIBIOS_BAD_REGISTER_NUMBER;
   }

   /* Read size bytes from config space in little-endian order */
   switch (size) {
   case 1:
      *val = cfg[where];
      break;
   case 2:
      *val = cfg[where] | ((u32)cfg[where + 1] << 8);
      break;
   case 4:
      *val = cfg[where] |
             ((u32)cfg[where + 1] << 8) |
             ((u32)cfg[where + 2] << 16) |
             ((u32)cfg[where + 3] << 24);
      break;
   default:
      *val = 0xFFFFFFFF;
      return PCIBIOS_BAD_REGISTER_NUMBER;
   }

   return PCIBIOS_SUCCESSFUL;
}

/**
 * emu_pci_write - PCI config space write callback
 * @bus:   PCI bus being written
 * @devfn: device/function number (only devfn 0 has our device)
 * @where: config space offset
 * @size:  write size in bytes (1, 2, or 4)
 * @val:   value to write
 *
 * Handles BAR sizing (write 0xFFFFFFFF / read back size mask) and
 * normal config space writes (command register, etc.).
 *
 * Return: PCIBIOS_SUCCESSFUL, PCIBIOS_DEVICE_NOT_FOUND, or
 *         PCIBIOS_BAD_REGISTER_NUMBER
 */
static int emu_pci_write(struct pci_bus *bus, unsigned int devfn,
                         int where, int size, u32 val)
{
   struct emu_pci_host *host = bus->sysdata;
   uint8_t *cfg = host->cfg_space;

   /* Only respond to device 0, function 0 */
   if (devfn != 0)
      return PCIBIOS_DEVICE_NOT_FOUND;

   /* Bounds check */
   if (where + size > EMU_PCI_CFG_SIZE)
      return PCIBIOS_BAD_REGISTER_NUMBER;

   /*
    * BAR0 sizing: when the PCI core writes 0xFFFFFFFF to BAR0 (offset 0x10),
    * it reads back the size mask. We respond with the complement of
    * (BAR_SIZE - 1) ORed with type bits (0x04 = 64-bit memory).
    * After sizing, the core writes the actual address back.
    *
    * Use host->bar->size (the actually-allocated size, which may be smaller
    * than the nominal 16MB if emu_bar0_alloc() fell back to a reduced order
    * under memory fragmentation) so the PCI core ioremap()s exactly the
    * pages we own.
    */
   if (where == EMU_CFG_BAR0 && size == 4) {
      if (val == 0xFFFFFFFF) {
         /* Sizing request: report actual BAR size as 64-bit memory BAR */
         uint32_t size_mask = ~((uint32_t)(host->bar->size - 1)) | 0x04;
         cfg_write32(cfg, EMU_CFG_BAR0, size_mask);
         return PCIBIOS_SUCCESSFUL;
      }
      /* Normal write: store the value (PCI core assigns address) */
      cfg_write32(cfg, EMU_CFG_BAR0, val);
      return PCIBIOS_SUCCESSFUL;
   }

   /*
    * BAR1 sizing: upper 32 bits of the 64-bit BAR0 address.
    * Write 0xFFFFFFFF returns 0xFFFFFFFF for the upper bits,
    * indicating the BAR fits within < 4GB (correct for 16MB).
    */
   if (where == EMU_CFG_BAR1 && size == 4) {
      if (val == 0xFFFFFFFF) {
         /* Sizing request: upper 32 bits all set (< 4GB BAR) */
         cfg_write32(cfg, EMU_CFG_BAR1, 0xFFFFFFFF);
         return PCIBIOS_SUCCESSFUL;
      }
      /* Normal write: store the upper address bits */
      cfg_write32(cfg, EMU_CFG_BAR1, val);
      return PCIBIOS_SUCCESSFUL;
   }

   /* All other registers: write size bytes in little-endian order */
   switch (size) {
   case 1:
      cfg[where] = val & 0xFF;
      break;
   case 2:
      cfg[where]     = val & 0xFF;
      cfg[where + 1] = (val >> 8) & 0xFF;
      break;
   case 4:
      cfg_write32(cfg, where, val);
      break;
   default:
      return PCIBIOS_BAD_REGISTER_NUMBER;
   }

   return PCIBIOS_SUCCESSFUL;
}

/* PCI operations for the virtual host bridge */
static struct pci_ops emu_pci_ops = {
   .read  = emu_pci_read,
   .write = emu_pci_write,
};

static int emu_pci_bus_notify(struct notifier_block *nb,
                              unsigned long action, void *data)
{
   struct device *dev = data;
   struct pci_dev *pdev;

   if (dev->bus != &pci_bus_type)
      return NOTIFY_DONE;

   pdev = to_pci_dev(dev);
   if (!g_emu_host || pdev->bus != g_emu_host->bus)
      return NOTIFY_DONE;

   if (action == BUS_NOTIFY_BIND_DRIVER) {
#ifdef CONFIG_DMA_CMA
      pdev->dev.cma_area = NULL;
#endif
      set_dev_node(&pdev->dev, NUMA_NO_NODE);
      pr_info("emu: cleared cma_area and numa_node on rebind for %s\n",
              pci_name(pdev));
   }
   return NOTIFY_DONE;
}

static struct notifier_block emu_pci_nb = {
   .notifier_call = emu_pci_bus_notify,
};

/**
 * emu_pci_host_create - Create virtual PCI host bridge and scan bus
 * @host: host bridge state structure (caller-allocated, zeroed)
 * @bar:  initialized BAR0 memory allocation
 * @virq: virtual IRQ number to assign to the emulated device
 *
 * Allocates a PCI host bridge, configures custom pci_ops for config space
 * emulation, scans the bus to enumerate the virtual device, and fixes up
 * the pci_dev->irq field before triggering driver probe via
 * pci_bus_add_devices().
 *
 * Return: 0 on success, negative errno on failure
 */
int emu_pci_host_create(struct emu_pci_host *host,
                        struct emu_bar0 *bar,
                        unsigned int virq)
{
   struct pci_dev *pdev;
   int ret;

   /* Store BAR0 and IRQ references */
   host->bar  = bar;
   host->virq = virq;

   /* Populate PCI config space with static values */
   emu_pci_init_cfg_space(host);

   /* Initialize the memory resource for BAR0 */
   host->mem_res.start = bar->phys;
   host->mem_res.end   = bar->phys + bar->size - 1;
   host->mem_res.flags = IORESOURCE_MEM;
   host->mem_res.name  = "emu-bar0";

   /* Allocate the PCI host bridge */
   host->bridge = pci_alloc_host_bridge(0);
   if (!host->bridge) {
      pr_err("emu: failed to allocate PCI host bridge\n");
      return -ENOMEM;
   }

   /* Configure the host bridge */
   host->bridge->ops     = &emu_pci_ops;
   host->bridge->sysdata = host;
   host->bridge->busnr   = 0;
   INIT_LIST_HEAD(&host->bridge->windows);
   pci_add_resource(&host->bridge->windows, &host->mem_res);

   /* Scan the bus to enumerate the virtual device */
   ret = pci_scan_root_bus_bridge(host->bridge);
   if (ret) {
      pr_err("emu: pci_scan_root_bus_bridge failed (%d)\n", ret);
      pci_free_host_bridge(host->bridge);
      host->bridge = NULL;
      return ret;
   }

   /* Get reference to the root bus */
   host->bus = host->bridge->bus;

   /*
    * Claim BAR resources under the bridge memory window.  Without this,
    * pci_dev->resource[0].parent remains NULL and the datadev driver's
    * pci_enable_device() call fails with -EINVAL ("BAR 0 ...: not
    * claimed; can't enable device").  This matches the standard PCI
    * host controller flow (see drivers/pci/controller/pci-host-common.c).
    */
   pci_bus_claim_resources(host->bus);

   /*
    * Fix pci_dev->irq: The PCI subsystem does NOT populate pcidev->irq
    * from config space byte 0x3C for virtual host bridges. The datadev
    * driver checks pcidev->irq != 0 at data_dev_top.c:270 and fails
    * probe if it is 0. Set the IRQ on all enumerated devices BEFORE
    * pci_bus_add_devices() triggers the driver probe path.
    */
   for_each_pci_dev(pdev) {
      if (pdev->bus == host->bus) {
         pdev->irq = virq;
         dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
         dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
#ifdef CONFIG_DMA_CMA
         pdev->dev.cma_area = NULL;
#endif
         set_dev_node(&pdev->dev, NUMA_NO_NODE);
         pr_info("emu: set pci_dev %s irq to %u, DMA mask 64-bit\n",
                 pci_name(pdev), virq);
      }
   }

   /* Complete device registration (triggers driver probe) */
   pci_bus_add_devices(host->bus);

   pr_info("emu: PCI host bridge created, bus %d\n",
           host->bus->number);

   return 0;
}

/**
 * emu_pci_host_destroy - Tear down the virtual PCI host bridge
 * @host: host bridge state to destroy
 *
 * Stops all devices on the root bus, removes the bus, and cleans up.
 * The pci_remove_root_bus call also frees the bridge structure.
 */
void emu_pci_host_destroy(struct emu_pci_host *host)
{
   if (!host->bus)
      return;

   pci_stop_root_bus(host->bus);
   pci_remove_root_bus(host->bus);

   host->bus    = NULL;
   host->bridge = NULL;

   pr_info("emu: PCI host bridge destroyed\n");
}

/**
 * emu_pci_notifier_init - Register a PCI bus notifier to re-clear per-device
 *                         CMA state on every driver bind.
 * @host: emu_pci_host whose bus is the filter target
 *
 * Stores the host pointer in a file-static for the callback's filter, then
 * registers the notifier_block on pci_bus_type. On BUS_NOTIFY_BIND_DRIVER
 * for any pci_dev on @host->bus, the callback re-zeroes pdev->dev.cma_area
 * (guarded by CONFIG_DMA_CMA) to prevent stale-pointer deref in __cma_alloc
 * on datadev rebind.
 *
 * Return: 0 on success, negative errno from bus_register_notifier on failure
 */
int emu_pci_notifier_init(struct emu_pci_host *host)
{
   int ret;

   g_emu_host = host;
   ret = bus_register_notifier(&pci_bus_type, &emu_pci_nb);
   if (ret) {
      pr_err("emu: bus_register_notifier failed (%d)\n", ret);
      g_emu_host = NULL;
      return ret;
   }
   pr_info("emu: PCI bus notifier registered\n");
   return 0;
}

/**
 * emu_pci_notifier_destroy - Unregister the PCI bus notifier.
 * @host: emu_pci_host used at init time (unused; kept for symmetric API)
 *
 * Idempotent: safe to call if init failed or was never called.
 */
void emu_pci_notifier_destroy(struct emu_pci_host *host)
{
   (void)host;
   if (g_emu_host == NULL)
      return;
   bus_unregister_notifier(&pci_bus_type, &emu_pci_nb);
   g_emu_host = NULL;
   pr_info("emu: PCI bus notifier unregistered\n");
}
