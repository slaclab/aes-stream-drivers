/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Virtual PCI-MSI / MSI-X domain plumbing for the FPGA emulator.
 *
 *    Real PCIe MSI delivery on a software-only virtual host bridge requires
 *    that the bridge expose an MSI irq_domain so the kernel's
 *    pci_alloc_irq_vectors() path actually returns a usable Linux IRQ
 *    instead of failing back to legacy INTx. This file builds two domains:
 *
 *      parent (linear, 16 hwirqs)
 *         -> child PCI-MSI domain (built by pci_msi_create_irq_domain)
 *
 *    The parent's .alloc op allocates a fresh hwirq, wires it to a
 *    software-only irq_chip whose ack/mask/unmask are no-ops (delivery
 *    happens via generic_handle_irq() from emu_msi_fire_safe(), not via a
 *    real interrupt controller). The child PCI-MSI domain wraps the parent
 *    using the kernel's default MSI domain/chip ops.
 *
 *    After pci_scan_root_bus_bridge() enumerates the virtual device, the
 *    host bridge attaches the child domain to the pci_dev via
 *    dev_set_msi_domain() -- before pci_bus_add_devices() triggers driver
 *    probe -- so when the datadev driver calls pci_alloc_irq_vectors(...
 *    PCI_IRQ_MSIX | PCI_IRQ_MSI | PCI_IRQ_INTX) the kernel finds an MSI
 *    domain to allocate from.
 *----------------------------------------------------------------------------
 * This file is part of the aes_stream_drivers package. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the aes_stream_drivers package, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 *----------------------------------------------------------------------------
 **/
#ifndef __EMU_VIRT_MSI_H__
#define __EMU_VIRT_MSI_H__

#include <linux/irqdomain.h>
#include <linux/irq_work.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/*
 * Maximum hwirqs the virtual MSI parent can hand out. The datadev driver
 * only ever requests 1 vector via pci_alloc_irq_vectors(... 1, 1, ...),
 * so 16 is generous future-proofing without bloating the linear domain.
 * hwirqs are recycled on .free (see hwirq_map below), so this bounds the
 * number of vectors live at once, not the number of load/unload cycles.
 */
#define EMU_MSI_HWIRQ_COUNT  16

/**
 * struct emu_msi - Virtual PCI-MSI domain state
 * @fwnode:        synthesized fwnode_handle owned by this domain
 * @parent_domain: linear hwirq->virq parent
 * @msi_domain:    PCI-MSI child domain (the one the kernel binds to a bus)
 * @alloc_virq:    most recently allocated child virq (used by emu_msi_fire)
 * @irq_work:      deferred fire helper, runs generic_handle_irq() in
 *                 hardirq context via self-IPI
 * @hwirq_lock:    protects hwirq_map against concurrent alloc/free
 * @hwirq_map:     bitmap of in-use parent hwirqs; recycled on .free so
 *                 repeated datadev reloads cannot exhaust the pool
 */
struct emu_msi {
   struct fwnode_handle *fwnode;
   struct irq_domain    *parent_domain;
   struct irq_domain    *msi_domain;
   unsigned int          alloc_virq;
   struct irq_work       irq_work;
   spinlock_t            hwirq_lock;
   DECLARE_BITMAP(hwirq_map, EMU_MSI_HWIRQ_COUNT);
};

/**
 * emu_msi_create - Build the virtual PCI-MSI parent + child domains.
 * @m: caller-allocated state structure (zero-initialized)
 *
 * Both domains are built unconditionally; the caller decides at host-bridge
 * creation time whether to attach the resulting MSI domain to the bus
 * (skip for "intx" mode, attach for "msi"/"msix" modes).
 *
 * Return: 0 on success, negative errno on failure
 */
int emu_msi_create(struct emu_msi *m);

/**
 * emu_msi_destroy - Tear down the virtual PCI-MSI domains.
 * @m: state structure
 *
 * Idempotent on partially-initialized state (NULL pointer checks throughout).
 */
void emu_msi_destroy(struct emu_msi *m);

/**
 * emu_msi_get_domain - Return the PCI-MSI child domain.
 * @m: state structure
 *
 * Caller passes this to the host bridge so the kernel can allocate
 * MSI/MSI-X vectors against it during pci_alloc_irq_vectors().
 *
 * Return: irq_domain pointer (or NULL if create() failed)
 */
struct irq_domain *emu_msi_get_domain(const struct emu_msi *m);

/**
 * emu_msi_fire_safe - Trigger the most recently allocated MSI virq.
 * @m: state structure
 *
 * Mirrors emu_irq_fire_safe (legacy INTx path) but targets the kernel-
 * allocated child virq instead of the linear parent. Safe to call from
 * any context; uses irq_work to defer the generic_handle_irq() call to
 * hardirq context.
 */
void emu_msi_fire_safe(struct emu_msi *m);

#endif /* __EMU_VIRT_MSI_H__ */
