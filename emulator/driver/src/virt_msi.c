/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Virtual PCI-MSI / MSI-X domain implementation. Provides a parent
 *    irq_domain backed by a software-only irq_chip and a child PCI-MSI
 *    domain built via pci_msi_create_irq_domain(), which the host bridge
 *    attaches to its bus so pci_alloc_irq_vectors() succeeds without real
 *    MSI hardware.
 *
 *    Delivery model: the kernel-allocated child virq is recorded on .alloc
 *    so emu_msi_fire_safe() can call generic_handle_irq() directly. The
 *    actual MSI memory writes the kernel programs into the device's MSI
 *    cap (or MSI-X table) are never observed -- they land in cfg_space[]
 *    or in the BAR0 RAM-backed page and are simply ignored.
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
#include <linux/bitmap.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irq_work.h>
#include <linux/kernel.h>
#include <linux/msi.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/version.h>

#include "virt_msi.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 19, 0)

/*
 * pci_msi_create_irq_domain() was removed in Linux 6.19 (the global PCI-MSI
 * domain model was replaced by per-device msi_create_parent_irq_domain()).
 * The emulator's virtual MSI/MSI-X path has not been ported to that API, so
 * emulated MSI/MSI-X is unsupported on these kernels. INTx mode -- the default
 * and the only mode exercised by the load/test phase of CI -- is unaffected,
 * and emu_main.c only calls emu_msi_create() when emu_irq_mode != intx.
 */
int emu_msi_create(struct emu_msi *m)
{
   m->alloc_virq    = 0;
   m->fwnode        = NULL;
   m->parent_domain = NULL;
   m->msi_domain    = NULL;
   pr_warn("emu_msi: virtual PCI-MSI/MSI-X unsupported on Linux >= 6.19 "
           "(pci_msi_create_irq_domain removed); use emu_irq_mode=intx\n");
   return -EOPNOTSUPP;
}

void emu_msi_destroy(struct emu_msi *m)
{
   /* emu_msi_create() allocates nothing on this kernel; nothing to free. */
}

#else  /* LINUX_VERSION_CODE < KERNEL_VERSION(6, 19, 0) */

/*
 * Software-only irq_chip for the parent domain. The kernel calls these
 * during request_irq() / free_irq() and during MSI mask/unmask. None of
 * them need to do anything: delivery is via generic_handle_irq() from
 * emu_msi_fire_safe(), and there's no real controller to ack/mask.
 */
static void emu_msi_irq_noop(struct irq_data *d)
{
   /* nothing to do */
}

/*
 * Software-only MSI message composer. Required by msi_domain_activate()'s
 * irq_chip_compose_msi_msg() contract -- it walks up the irq hierarchy
 * and BUG_ON()s if no chip in the chain implements this op. We fill with
 * zeros: the kernel writes the result into the device's MSI cap (or
 * MSI-X table entry), but the emulator never observes those writes --
 * delivery is via generic_handle_irq() from emu_msi_fire_safe(), not
 * via the message-address/data path that real MSI uses.
 */
static void emu_msi_compose_msg(struct irq_data *d, struct msi_msg *msg)
{
   msg->address_lo = 0;
   msg->address_hi = 0;
   msg->data       = 0;
}

/*
 * Software-only affinity setter. msi_domain_set_affinity() unconditionally
 * dereferences parent->chip->irq_set_affinity during irq_startup() (called
 * from request_threaded_irq()); a NULL slot crashes the kernel with
 * RIP=0x0. Since delivery is via generic_handle_irq() from arbitrary CPU
 * context (irq_work IPI / poll thread), affinity has no real meaning here
 * -- accept any mask and return NOCOPY so the kernel does not re-write the
 * irq_data's effective_mask.
 */
static int emu_msi_set_affinity(struct irq_data *d, const struct cpumask *m,
                                bool force)
{
   return IRQ_SET_MASK_OK_NOCOPY;
}

static struct irq_chip emu_msi_parent_chip = {
   .name                = "EMU-MSI-PARENT",
   .irq_ack             = emu_msi_irq_noop,
   .irq_mask            = emu_msi_irq_noop,
   .irq_unmask          = emu_msi_irq_noop,
   .irq_eoi             = emu_msi_irq_noop,
   .irq_compose_msi_msg = emu_msi_compose_msg,
   .irq_set_affinity    = emu_msi_set_affinity,
   .flags               = IRQCHIP_SKIP_SET_WAKE,
};

/**
 * emu_msi_parent_alloc - Allocate hwirqs in the parent domain.
 *
 * Called by the kernel's PCI-MSI infrastructure when datadev calls
 * pci_alloc_irq_vectors(). For each requested vector, we associate the
 * caller-supplied virq with a fresh hwirq and our software chip.
 *
 * hwirqs come from a recycled bitmap (m->hwirq_map) rather than a
 * monotonic counter: the emulator reloads datadev many times within one
 * module lifetime, and a counter would leak a hwirq per cycle and
 * eventually fail pci_alloc_irq_vectors() even though every prior vector
 * was freed. On the -ENOSPC path no bits are set, so a failed attempt
 * leaves the pool untouched.
 *
 * The most recently allocated child virq is stashed in the
 * emu_msi state (passed via host_data) so the DMA engine can fire it
 * without snooping the bound pci_dev's resources.
 */
static int emu_msi_parent_alloc(struct irq_domain *d, unsigned int virq,
                                unsigned int nr_irqs, void *arg)
{
   struct emu_msi *m = d->host_data;
   unsigned int i;
   unsigned long flags;
   unsigned long hw_base;

   if (m == NULL)
      return -EINVAL;

   spin_lock_irqsave(&m->hwirq_lock, flags);
   hw_base = bitmap_find_next_zero_area(m->hwirq_map, EMU_MSI_HWIRQ_COUNT,
                                        0, nr_irqs, 0);
   if (hw_base >= EMU_MSI_HWIRQ_COUNT) {
      spin_unlock_irqrestore(&m->hwirq_lock, flags);
      pr_err("emu_msi: hwirq pool exhausted (req=%u cap=%d)\n",
             nr_irqs, EMU_MSI_HWIRQ_COUNT);
      return -ENOSPC;
   }
   bitmap_set(m->hwirq_map, hw_base, nr_irqs);
   spin_unlock_irqrestore(&m->hwirq_lock, flags);

   for (i = 0; i < nr_irqs; i++) {
      irq_domain_set_info(d, virq + i, hw_base + i,
                          &emu_msi_parent_chip,
                          NULL,                /* chip_data */
                          handle_simple_irq,
                          NULL, NULL);
   }

   /* Track the first allocated virq for the DMA engine fire path. The
    * datadev driver always allocates a single vector, so virq is the
    * one that ends up in pdev->irq via pci_irq_vector(pdev, 0). */
   if (m != NULL)
      m->alloc_virq = virq;

   return 0;
}

static void emu_msi_parent_free(struct irq_domain *d, unsigned int virq,
                                unsigned int nr_irqs)
{
   struct emu_msi *m = d->host_data;
   struct irq_data *irqd = irq_get_irq_data(virq);
   unsigned long flags;
   unsigned int i;

   /* Return the contiguous hwirq run to the pool. The run is laid out
    * hw_base..hw_base+nr_irqs by alloc, so the first virq's hwirq is the
    * base. Recover it before irq_domain_reset_irq_data() clears it. */
   if (m != NULL && irqd != NULL) {
      spin_lock_irqsave(&m->hwirq_lock, flags);
      bitmap_clear(m->hwirq_map, irqd->hwirq, nr_irqs);
      spin_unlock_irqrestore(&m->hwirq_lock, flags);
   }

   for (i = 0; i < nr_irqs; i++)
      irq_domain_reset_irq_data(irq_get_irq_data(virq + i));

   if (m != NULL && m->alloc_virq >= virq && m->alloc_virq < virq + nr_irqs)
      m->alloc_virq = 0;
}

static const struct irq_domain_ops emu_msi_parent_ops = {
   .alloc = emu_msi_parent_alloc,
   .free  = emu_msi_parent_free,
};

/*
 * msi_domain_info passed to pci_msi_create_irq_domain. The default ops
 * supplied by the kernel handle the MSI mask/unmask plumbing on top of
 * our parent chip; MSI_FLAG_PCI_MSIX additionally allows MSI-X mode.
 */
static struct irq_chip emu_msi_chip = {
   .name = "EMU-MSI",
};

static struct msi_domain_info emu_msi_domain_info = {
   .flags = MSI_FLAG_USE_DEF_DOM_OPS |
            MSI_FLAG_USE_DEF_CHIP_OPS |
            MSI_FLAG_PCI_MSIX,
   .chip  = &emu_msi_chip,
};

static void emu_msi_work_fn(struct irq_work *work)
{
   struct emu_msi *m = container_of(work, struct emu_msi, irq_work);

   if (m->alloc_virq != 0)
      generic_handle_irq(m->alloc_virq);
}

int emu_msi_create(struct emu_msi *m)
{
   m->alloc_virq = 0;
   spin_lock_init(&m->hwirq_lock);
   bitmap_zero(m->hwirq_map, EMU_MSI_HWIRQ_COUNT);

   m->fwnode = irq_domain_alloc_named_fwnode("emu-msi");
   if (m->fwnode == NULL) {
      pr_err("emu_msi: irq_domain_alloc_named_fwnode failed\n");
      return -ENOMEM;
   }

   m->parent_domain = irq_domain_create_linear(m->fwnode,
                                               EMU_MSI_HWIRQ_COUNT,
                                               &emu_msi_parent_ops, m);
   if (m->parent_domain == NULL) {
      pr_err("emu_msi: parent irq_domain_create_linear failed\n");
      irq_domain_free_fwnode(m->fwnode);
      m->fwnode = NULL;
      return -ENOMEM;
   }
   /* IRQ_DOMAIN_FLAG_MSI_PARENT is a 6.0+-only flag and is informational
    * for hierarchical multi-bus MSI controllers. A simple software parent
    * directly underneath pci_msi_create_irq_domain doesn't need it; omit
    * for cross-kernel-version portability across the CI matrix. */

   m->msi_domain = pci_msi_create_irq_domain(m->fwnode,
                                             &emu_msi_domain_info,
                                             m->parent_domain);
   if (m->msi_domain == NULL) {
      pr_err("emu_msi: pci_msi_create_irq_domain failed\n");
      irq_domain_remove(m->parent_domain);
      m->parent_domain = NULL;
      irq_domain_free_fwnode(m->fwnode);
      m->fwnode = NULL;
      return -ENOMEM;
   }

   init_irq_work(&m->irq_work, emu_msi_work_fn);

   pr_info("emu_msi: PCI-MSI domain ready (parent=%p child=%p)\n",
           m->parent_domain, m->msi_domain);
   return 0;
}

void emu_msi_destroy(struct emu_msi *m)
{
   /* Stop new fires, then wait for any irq_work queued by a last-gasp
    * emu_msi_fire_safe() to finish running. Without this the deferred
    * emu_msi_work_fn() could execute after the domains are torn down --
    * or after the module text is freed -- and call generic_handle_irq()
    * on a stale virq. Clearing alloc_virq first makes any already-queued
    * work a no-op; irq_work_sync() then guarantees it has returned.
    * Safe on partially-initialized state: a zero-initialized irq_work
    * has no pending flags, so irq_work_sync() returns immediately. */
   m->alloc_virq = 0;
   irq_work_sync(&m->irq_work);

   if (m->msi_domain != NULL) {
      irq_domain_remove(m->msi_domain);
      m->msi_domain = NULL;
   }
   if (m->parent_domain != NULL) {
      irq_domain_remove(m->parent_domain);
      m->parent_domain = NULL;
   }
   if (m->fwnode != NULL) {
      irq_domain_free_fwnode(m->fwnode);
      m->fwnode = NULL;
   }
}

#endif  /* LINUX_VERSION_CODE < KERNEL_VERSION(6, 19, 0) */

struct irq_domain *emu_msi_get_domain(const struct emu_msi *m)
{
   return m->msi_domain;
}

void emu_msi_fire_safe(struct emu_msi *m)
{
   if (m->alloc_virq != 0)
      irq_work_queue(&m->irq_work);
}
