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
 * Linux 6.19 removed pci_msi_create_irq_domain() and the global PCI-MSI
 * domain model. A controller now creates only a single MSI *parent* domain
 * via msi_create_parent_irq_domain(); the PCI core auto-creates the
 * per-device MSI/MSI-X child from its built-in templates when
 * dev_set_msi_domain() points a pdev at the parent. This is the ported
 * equivalent of the pre-6.19 parent+child construction below.
 *
 * The bottom-domain alloc/free, the software irq_chip, the recycled hwirq
 * bitmap, and the generic_handle_irq() delivery path are unchanged from the
 * legacy implementation -- only the domain plumbing differs.
 */

/*
 * Software-only MSI message composer. msi_domain_activate() walks the irq
 * hierarchy via irq_chip_compose_msi_msg() and requires a composer somewhere
 * in the chain; the PCI-core child chip only supplies irq_write_msi_msg, so
 * the parent must provide this. We fill zeros: the kernel writes the result
 * into the device's MSI cap / MSI-X table, but the emulator never observes
 * those writes -- delivery is via generic_handle_irq() from emu_msi_fire_safe().
 */
static void emu_msi_compose_msg(struct irq_data *d, struct msi_msg *msg)
{
   msg->address_lo = 0;
   msg->address_hi = 0;
   msg->data       = 0;
}

/*
 * Minimal parent irq_chip. With MSI_FLAG_NO_AFFINITY in the parent ops the
 * core never dereferences a set_affinity slot, so the legacy affinity/ack/
 * mask noop stubs are unnecessary here.
 */
static struct irq_chip emu_msi_parent_chip = {
   .name                = "EMU-MSI-PARENT",
   .irq_compose_msi_msg = emu_msi_compose_msg,
};

/**
 * emu_msi_parent_alloc - Allocate hwirqs in the parent (bottom) domain.
 *
 * Called (recursively, via the PCI-core child) when datadev calls
 * pci_alloc_irq_vectors(). Identical bitmap logic to the pre-6.19 path:
 * hwirqs come from a recycled bitmap so repeated datadev reloads within one
 * emulator lifetime cannot leak the pool. On -ENOSPC no bits are set.
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

   for (i = 0; i < nr_irqs; i++)
      irq_domain_set_info(d, virq + i, hw_base + i,
                          &emu_msi_parent_chip,
                          NULL,                /* chip_data */
                          handle_simple_irq,
                          NULL, NULL);

   /* datadev always allocates a single vector, so virq ends up in
    * pdev->irq via pci_irq_vector(pdev, 0). */
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
 * Per-device child initializer. The PCI core calls this to build each
 * device's MSI/MSI-X child domain from the parent. Modeled on
 * msi_lib_init_dev_msi_info() but supplied locally so the module depends
 * only on CONFIG_PCI_MSI, not CONFIG_IRQ_MSI_LIB (a hidden bool that stock
 * x86_64 kernels do not select -- msi_lib_init_dev_msi_info would be an
 * unresolved symbol at load time).
 */
static bool emu_msi_init_dev_msi_info(struct device *dev,
                                      struct irq_domain *domain,
                                      struct irq_domain *real_parent,
                                      struct msi_domain_info *info)
{
   const struct msi_parent_ops *pops = real_parent->msi_parent_ops;

   switch (info->bus_token) {
   case DOMAIN_BUS_PCI_DEVICE_MSI:
   case DOMAIN_BUS_PCI_DEVICE_MSIX:
      break;
   default:
      return false;
   }

   info->flags &= pops->supported_flags;   /* clamp to what we support */
   info->flags |= pops->required_flags;     /* enforce USE_DEF_*_OPS etc. */
   return true;
}

/*
 * USE_DEF_DOM_OPS | USE_DEF_CHIP_OPS are mandatory: without them the
 * auto-created child domain has no alloc/activate/chip ops and allocation
 * crashes. NO_AFFINITY lets us omit a parent set_affinity. PCI_MSIX permits
 * MSI-X mode.
 */
#define EMU_MSI_FLAGS_REQUIRED  (MSI_FLAG_USE_DEF_DOM_OPS  | \
                                 MSI_FLAG_USE_DEF_CHIP_OPS | \
                                 MSI_FLAG_NO_AFFINITY)
#define EMU_MSI_FLAGS_SUPPORTED (MSI_GENERIC_FLAGS_MASK | MSI_FLAG_PCI_MSIX)

static const struct msi_parent_ops emu_msi_parent_msi_ops = {
   .required_flags    = EMU_MSI_FLAGS_REQUIRED,
   .supported_flags   = EMU_MSI_FLAGS_SUPPORTED,
   .bus_select_token  = DOMAIN_BUS_PCI_MSI,
   .prefix            = "EMU-",
   .init_dev_msi_info = emu_msi_init_dev_msi_info,
};

/* Deferred fire helper: runs generic_handle_irq() on the allocated child
 * virq in hardirq context via self-IPI. Identical to the pre-6.19 copy. */
static void emu_msi_work_fn(struct irq_work *work)
{
   struct emu_msi *m = container_of(work, struct emu_msi, irq_work);

   if (m->alloc_virq != 0)
      generic_handle_irq(m->alloc_virq);
}

int emu_msi_create(struct emu_msi *m)
{
   struct irq_domain_info info;

   m->alloc_virq = 0;
   spin_lock_init(&m->hwirq_lock);
   bitmap_zero(m->hwirq_map, EMU_MSI_HWIRQ_COUNT);

   m->fwnode = irq_domain_alloc_named_fwnode("emu-msi");
   if (m->fwnode == NULL) {
      pr_err("emu_msi: irq_domain_alloc_named_fwnode failed\n");
      return -ENOMEM;
   }

   /* msi_create_parent_irq_domain() sets IRQ_DOMAIN_FLAG_MSI_PARENT and
    * bus_token internally from the parent ops -- do not set them here. size
    * becomes hwirq_max inside the helper. */
   info = (struct irq_domain_info){
      .fwnode    = m->fwnode,
      .ops       = &emu_msi_parent_ops,
      .host_data = m,
      .size      = EMU_MSI_HWIRQ_COUNT,
   };

   m->parent_domain = msi_create_parent_irq_domain(&info,
                                                   &emu_msi_parent_msi_ops);
   if (m->parent_domain == NULL) {
      pr_err("emu_msi: msi_create_parent_irq_domain failed\n");
      irq_domain_free_fwnode(m->fwnode);
      m->fwnode = NULL;
      return -ENOMEM;
   }

   /* Single domain now: the PCI core auto-creates the per-device child from
    * the parent when dev_set_msi_domain() attaches it. emu_msi_get_domain()
    * hands this to the host bridge just as before. */
   m->msi_domain = m->parent_domain;

   init_irq_work(&m->irq_work, emu_msi_work_fn);

   pr_info("emu_msi: PCI-MSI parent domain ready (parent=%p)\n",
           m->parent_domain);
   return 0;
}

void emu_msi_destroy(struct emu_msi *m)
{
   /* Stop new fires, then drain any irq_work queued by a last-gasp
    * emu_msi_fire_safe() before tearing the domain down (see the pre-6.19
    * emu_msi_destroy for the full rationale). */
   m->alloc_virq = 0;
   irq_work_sync(&m->irq_work);

   if (m->parent_domain != NULL) {
      irq_domain_remove(m->parent_domain);   /* single domain now */
      m->parent_domain = NULL;
      m->msi_domain    = NULL;
   }
   if (m->fwnode != NULL) {
      irq_domain_free_fwnode(m->fwnode);
      m->fwnode = NULL;
   }
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
