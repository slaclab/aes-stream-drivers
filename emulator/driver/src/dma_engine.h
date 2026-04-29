/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    DMA loopback engine for the PCI device emulator.
 *
 *    Hard-wired to 128-bit descriptor mode (Desc128En=1 in enableVer bit
 *    16). 64-bit descriptor mode is not emulated and not supported by
 *    this project going forward.
 *
 *    WriteFree capture: busy-poll writeFifoA (last-written register) to
 *    catch as many of the ~1024 init-time registrations as possible.
 *    WriteTx capture: poll readFifoB (size, always non-zero) in steady
 *    state to detect individual TX submissions.
 *    Completion rings use 16-byte entries per AxisG2_MapReturn.
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
#ifndef __EMU_DMA_ENGINE_H__
#define __EMU_DMA_ENGINE_H__

#include <linux/types.h>
#include <linux/kthread.h>
#include <linux/spinlock.h>
#include "bar0_regs.h"
#include "virt_irq.h"

/* Maximum RX buffers in free pool (matches driver's default rxBuffers.count) */
#define EMU_FREE_POOL_MAX   4096

/* Polling interval in microseconds (steady-state) */
#define EMU_POLL_INTERVAL_US  1000

/* Timeout for init poll in milliseconds.  Kept short because the seed
 * phase (post-init) reliably discovers RX buffers even if the init poll
 * captures none from the lossy shadow-register WriteFree path. */
#define EMU_INIT_POLL_TIMEOUT_MS  100

/* Register offsets relative to AGEN2 base (BAR0 + EMU_AGEN2_OFF) */
#define EMU_REG_ENABLEVER     0x0000
#define EMU_REG_INTENABLE     0x0004
#define EMU_REG_WRBASELOW     0x0010
#define EMU_REG_WRBASEHIGH    0x0014
#define EMU_REG_RDBASELOW     0x0018
#define EMU_REG_RDBASEHIGH    0x001C
#define EMU_REG_FIFORESET     0x0020
#define EMU_REG_ONLINE        0x002C
#define EMU_REG_ADDRWIDTH     0x0038
#define EMU_REG_READFIFOA     0x0040  /* TX: flags | chan | dest        */
#define EMU_REG_READFIFOB     0x0044  /* TX: size (detection register)  */
#define EMU_REG_WRITEFIFOA    0x0048  /* Free: index | addr_lo bits     */
#define EMU_REG_INTACKENA     0x004C
#define EMU_REG_HWWRINDEX     0x0054
#define EMU_REG_HWRDINDEX     0x0058
#define EMU_REG_READFIFOC     0x0060  /* TX: index | addr_lo bits       */
#define EMU_REG_READFIFOD     0x0064  /* TX: addr_hi bits               */
#define EMU_REG_WRITEFIFOB    0x0070  /* Free: addr_hi bits             */
#define EMU_REG_FORCEINT      0x0080

/**
 * struct emu_free_buf - Free buffer pool entry
 * @index:  Buffer index as registered by the driver
 * @handle: DMA handle (physical address) of the buffer
 */
struct emu_free_buf {
   uint32_t index;
   dma_addr_t handle;
};

/**
 * struct emu_dma_engine - DMA loopback engine state
 */
struct emu_dma_engine {
   /* Back-references */
   struct emu_bar0 *bar;
   struct emu_irq *irq;

   /* Register base (bar->virt + EMU_AGEN2_OFF) */
   void *reg_base;

   /* Ring buffer addresses (captured from driver writes to BAR0) */
   uint32_t *rd_ring;
   uint32_t *wr_ring;
   uint32_t rd_ring_idx;
   uint32_t wr_ring_idx;
   uint32_t addr_count;

   /* Free RX buffer pool */
   struct emu_free_buf free_pool[EMU_FREE_POOL_MAX];
   uint32_t free_count;
   spinlock_t free_lock;

   /* Polling kthread */
   struct task_struct *poll_thread;
   bool enabled;
   bool rings_valid;
   bool init_polling;

   /* enableVer R/O field enforcement state (see emu_enforce_enablever_ro).
    * prev_enable mirrors the last-observed bit 0 so the poll thread can
    * detect 0->1 edges driven by AxisG2_Enable and increment enable_cnt
    * (the hardware's enableCnt field, bits 15:8 of enableVer). Both live
    * for the emulator module's lifetime so the count accumulates across
    * driver load/unload cycles, matching VHDL behaviour. */
   uint8_t prev_enable;
   uint8_t enable_cnt;

   /* online 1->0 edge detection (driver AxisG2_Clear).  On the falling
    * edge the poll thread zeros the rd/wr base-address regs, invalidates
    * cached ring pointers, and marks rings_valid=false so the next
    * emu_capture_rings() cannot re-acquire the soon-to-be-freed DMA
    * addresses.  Initialized to 1 to match EMU_REG_ONLINE_INIT. */
   uint8_t prev_online;

   /* RX buffer seeding: after init busy-poll, write zero-size RX
    * completions one at a time to discover RX buffer indices/handles
    * from the driver's WriteFree responses. */
   bool seed_active;
   bool seed_pending;         /* waiting for driver to process current seed */
   uint32_t seed_idx;         /* next index to probe */
   uint32_t seed_max;         /* upper bound for probing */
   uint32_t seed_target;      /* desired free pool count */

   /* Statistics */
   uint32_t tx_count;
   uint32_t rx_count;
   uint32_t irq_count;
   uint32_t free_captured;
};

int emu_dma_init(struct emu_dma_engine *eng, struct emu_bar0 *bar,
                 struct emu_irq *irq);
void emu_dma_start(struct emu_dma_engine *eng);
void emu_dma_stop(struct emu_dma_engine *eng);
void emu_dma_destroy(struct emu_dma_engine *eng);

#endif /* __EMU_DMA_ENGINE_H__ */
