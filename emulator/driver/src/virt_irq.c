/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Virtual IRQ domain and interrupt allocation implementation.
 *    Creates a linear IRQ domain with a single hardware IRQ mapped to a
 *    Linux virtual IRQ number. The datadev driver assigns pcidev->irq from
 *    this virq and later calls request_irq(virq, ..., IRQF_SHARED, ...).
 *
 *    Phase 1: IRQ exists and is requestable. emu_irq_fire() is a stub
 *    that calls generic_handle_irq() but is never invoked.
 *    Phase 2: emu_irq_fire() is called from DMA loopback completion.
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
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irq_work.h>
#include <linux/kernel.h>

#include "virt_irq.h"

/**
 * emu_irq_map - Map a hardware IRQ to a virtual IRQ
 * @d:     IRQ domain
 * @virq:  Virtual IRQ number being mapped
 * @hwirq: Hardware IRQ number
 *
 * Sets up the virtual IRQ with the kernel's dummy_irq_chip (a no-op chip
 * for software-only IRQs) and handle_simple_irq as the flow handler.
 *
 * Return: 0 on success
 */
static int emu_irq_map(struct irq_domain *d, unsigned int virq,
                       irq_hw_number_t hwirq)
{
   irq_set_chip_and_handler(virq, &dummy_irq_chip, handle_simple_irq);
   irq_set_noprobe(virq);
   return 0;
}

/**
 * emu_irq_unmap - Unmap a virtual IRQ
 * @d:    IRQ domain
 * @virq: Virtual IRQ number being unmapped
 *
 * Clears the chip and handler for the virtual IRQ.
 */
static void emu_irq_unmap(struct irq_domain *d, unsigned int virq)
{
   irq_set_chip_and_handler(virq, NULL, NULL);
}

static const struct irq_domain_ops emu_irq_domain_ops = {
   .map   = emu_irq_map,
   .unmap = emu_irq_unmap,
};

/**
 * emu_irq_work_fn - irq_work callback for deferred IRQ injection
 * @work: irq_work item embedded in struct emu_irq
 *
 * Runs in hardirq context via self-IPI. Calls generic_handle_irq()
 * to invoke the ISR registered by the datadev driver.
 */
static void emu_irq_work_fn(struct irq_work *work)
{
   struct emu_irq *irq = container_of(work, struct emu_irq, irq_work);

   if (irq->virq != 0)
      generic_handle_irq(irq->virq);
}

/**
 * emu_irq_create - Allocate an IRQ domain and virtual IRQ
 * @irq: IRQ state structure to initialize
 *
 * Creates a linear IRQ domain supporting EMU_IRQ_COUNT hardware IRQs,
 * then maps hwirq 0 to a Linux virtual IRQ number.
 *
 * Return: 0 on success, -ENOMEM if domain creation fails,
 *         -EINVAL if IRQ mapping fails
 */
int emu_irq_create(struct emu_irq *irq)
{
   irq->hwirq = 0;

   /* Create a linear IRQ domain with no device-tree node */
   irq->domain = irq_domain_add_linear(NULL, EMU_IRQ_COUNT,
                                        &emu_irq_domain_ops, irq);
   if (irq->domain == NULL) {
      pr_err("emu: failed to create IRQ domain\n");
      return -ENOMEM;
   }

   /* Map hwirq 0 to a virtual IRQ number */
   irq->virq = irq_create_mapping(irq->domain, 0);
   if (irq->virq == 0) {
      pr_err("emu: failed to create IRQ mapping\n");
      irq_domain_remove(irq->domain);
      irq->domain = NULL;
      return -EINVAL;
   }

   /* Initialize irq_work for safe process-context IRQ injection */
   init_irq_work(&irq->irq_work, emu_irq_work_fn);

   pr_info("emu: virtual IRQ %u allocated\n", irq->virq);
   return 0;
}

/**
 * emu_irq_destroy - Tear down the IRQ domain and virtual IRQ
 * @irq: IRQ state structure to clean up
 *
 * Disposes the virtual IRQ mapping, removes the IRQ domain, and
 * zeroes the state fields. Safe to call on an uninitialized or
 * already-destroyed structure (NULL domain check).
 */
void emu_irq_destroy(struct emu_irq *irq)
{
   if (irq->domain == NULL)
      return;

   if (irq->virq != 0)
      irq_dispose_mapping(irq->virq);

   irq_domain_remove(irq->domain);

   irq->domain = NULL;
   irq->virq = 0;

   pr_info("emu: virtual IRQ destroyed\n");
}

/**
 * emu_irq_fire - Trigger the virtual interrupt (hardirq context only)
 * @irq: IRQ state structure
 *
 * Calls generic_handle_irq() to invoke the ISR registered by the datadev
 * driver on this virtual IRQ. Must be called from hardirq context only.
 * For process context callers (e.g., workqueues), use emu_irq_fire_safe()
 * instead.
 */
void emu_irq_fire(struct emu_irq *irq)
{
   if (irq->virq != 0)
      generic_handle_irq(irq->virq);
}

/**
 * emu_irq_fire_safe - Trigger the virtual interrupt from any context
 * @irq: IRQ state structure
 *
 * Uses the irq_work API to defer the generic_handle_irq() call to proper
 * hardirq context. Safe to call from process context (workqueues, kthreads).
 * The actual IRQ delivery happens asynchronously via a self-IPI on x86.
 */
void emu_irq_fire_safe(struct emu_irq *irq)
{
   if (irq->virq != 0)
      irq_work_queue(&irq->irq_work);
}
