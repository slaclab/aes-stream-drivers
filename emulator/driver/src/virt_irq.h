/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Virtual IRQ domain and interrupt allocation for the PCI device emulator.
 *    Creates a valid Linux virtual IRQ number that the datadev driver can use
 *    with request_irq(IRQF_SHARED) during probe.
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
#ifndef __EMU_VIRT_IRQ_H__
#define __EMU_VIRT_IRQ_H__

#include <linux/irqdomain.h>
#include <linux/irq_work.h>

#define EMU_IRQ_COUNT    1    /* Single IRQ for the emulated device */

/**
 * struct emu_irq - Virtual IRQ state for the emulated PCI device
 * @domain:  IRQ domain for virtual IRQs
 * @virq:    Linux virtual IRQ number
 * @hwirq:   Hardware IRQ number (always 0 for the emulator)
 */
struct emu_irq {
   struct irq_domain *domain;   /* IRQ domain for virtual IRQs */
   unsigned int virq;           /* Linux virtual IRQ number */
   int hwirq;                   /* Hardware IRQ number (0) */
   struct irq_work irq_work;    /* Deferred IRQ injection work */
};

/**
 * emu_irq_create - Allocate an IRQ domain and virtual IRQ
 * @irq: IRQ state structure to initialize
 *
 * Creates a linear IRQ domain and maps a single hardware IRQ (hwirq 0)
 * to a Linux virtual IRQ number. The resulting virq can be assigned to
 * pcidev->irq so the datadev driver's request_irq() succeeds.
 *
 * Return: 0 on success, negative errno on failure
 */
int emu_irq_create(struct emu_irq *irq);

/**
 * emu_irq_destroy - Tear down the IRQ domain and virtual IRQ
 * @irq: IRQ state structure to clean up
 *
 * Disposes the virtual IRQ mapping and removes the IRQ domain.
 * Safe to call if the domain was never created (NULL check).
 */
void emu_irq_destroy(struct emu_irq *irq);

/**
 * emu_irq_fire - Trigger the virtual interrupt (hardirq context only)
 * @irq: IRQ state structure
 *
 * Calls generic_handle_irq() to invoke any ISR registered on the virtual
 * IRQ. Must be called from hardirq context. For process context callers
 * (e.g., workqueues), use emu_irq_fire_safe() instead.
 */
void emu_irq_fire(struct emu_irq *irq);

/**
 * emu_irq_fire_safe - Trigger the virtual interrupt from any context
 * @irq: IRQ state structure
 *
 * Uses irq_work to defer the generic_handle_irq() call to hardirq context.
 * Safe to call from process context (workqueues, kthreads). The actual
 * IRQ delivery happens asynchronously via a self-IPI on x86.
 */
void emu_irq_fire_safe(struct emu_irq *irq);

#endif /* __EMU_VIRT_IRQ_H__ */
