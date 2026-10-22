/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    GPU Async engine for the PCI device emulator.
 *
 *    Functional mirror of AxiPcieGpuAsyncControl.vhd.  Polls at ~1 ms
 *    and runs two half-FSMs:
 *      - RX: drains writeFreeList[], generates a PRBS frame into the
 *        per-buffer kernel-virtual-address returned by
 *        emu_gpu_addr_lookup(), writes the descriptor / size doorbell,
 *        clears the free-list bit, advances nextWriteIdx, bumps
 *        rxFrameCnt.
 *      - TX: drains readReqSize[], verifies the egress PRBS payload,
 *        writes 1 (TX ACK doorbell) at remoteReadAddr + 0, clears the
 *        request word, advances nextReadIdx, bumps txFrameCnt.
 *
 *    Address translation uses an opaque `extern void *emu_gpu_addr_lookup
 *    (u64 fake_dma)`.  The real definition is supplied via nvidia_p2p_stub;
 *    a __weak fallback that returns NULL allows the module to load
 *    stand-alone (engine no-ops cleanly).
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
#ifndef __EMU_GPU_ENGINE_H__
#define __EMU_GPU_ENGINE_H__

#include <linux/types.h>
#include <linux/kthread.h>
#include <linux/spinlock.h>
#include "bar0_regs.h"

/* V4/V5 register offsets inside the GPU Async window (EMU_GPU_ASYNC_OFF +
 * these offsets, all from AxiPcieGpuAsyncControl.vhd). */
#define EMU_GPU_REG_MAX_BUFFERS   0x0000  /* [10:0]                     */
#define EMU_GPU_REG_CTRL          0x0008  /* writeCount [14:0],         */
                                          /* writeEnable [15],          */
                                          /* readCount [30:16],         */
                                          /* readEnable [31]            */
#define EMU_GPU_REG_RX_FRAME_CNT  0x0010
#define EMU_GPU_REG_TX_FRAME_CNT  0x0014
#define EMU_GPU_REG_CNT_RST       0x0020  /* write any non-zero => clear */
                                          /* rx/tx frame counts          */
#define EMU_GPU_REG_VERSION       0x0030
/* Write/read "active" readback (V5, axi-pcie-core v6.8.1). Mirrors the FW's
 * r.writeActive/r.readActive: bit0 = writeActive, bit1 = readActive. The
 * driver's V5 teardown (gpu_async.c:370) disables the engines then spins on
 * this until it reads 0, guaranteeing no in-flight RDMA before it frees
 * buffers. The poll thread keeps it in sync with the CTRL enable bits. */
#define EMU_GPU_REG_ACTIVE        0x0044  /* bit0 writeActive, bit1 readActive */
#define EMU_GPU_REG_RW_MAX_SIZE   0x0060

#define EMU_GPU_FREELIST_BASE     0x2000  /* writeFreeList[i] @ +i*4    */
#define EMU_GPU_READREQ_BASE      0x3000  /* readReqSize[i]   @ +i*4    */
#define EMU_GPU_WRADDR_BASE       0x4000  /* write addr[i]    @ +i*8    */
#define EMU_GPU_RDADDR_BASE       0x6000  /* read addr[i]     @ +i*8    */

/* Per-buffer payload layout written by the RX engine (V4):
 *   [0..3]        : AxiWrDesc64_t header word (reserved)
 *   [4..7]        : size (u32, non-zero) -- this is the doorbell the GPU polls
 *                   (rdmaTest.cu: cuStreamWaitValue32(rxBuffs[i] + 4, 1, GEQ))
 *   [DATA_BYTES_C..] : PRBS payload
 */
#define EMU_GPU_DATA_BYTES_C   16   /* DATA_BYTES_C from common/driver */

struct emu_gpu_engine {
    struct emu_bar0 *bar;

    /* Pointer to BAR0 + EMU_GPU_ASYNC_OFF -- usable with ioread32/iowrite32.
     * Backed by bar->uc_virt (UC ioremap) so writes are coherent with the
     * datadev driver's UC mapping of the same physical BAR. */
    void __iomem    *reg_base;

    /* Thread + lifecycle flags. */
    struct task_struct *poll_thread;
    bool                enabled;

    /* Sizing snapshot taken at enable-edge (from writeCount/readCount). */
    u32 write_count;
    u32 read_count;

    /* Round-robin cursors (VHDL: nextWriteIdx, nextReadIdx). */
    u32 next_write_idx;
    u32 next_read_idx;

    /* PRBS generator sequence for the RX direction. Fed as the seed into
     * emu_prbs_gen_data() on every RX tick; the return value advances it
     * to the next sequence so consecutive frames produce a continuous
     * LFSR stream the userspace verifier can match. No TX-side counter
     * is needed because the verifier reads the sequence out of the
     * incoming frame header (data32[0]) rather than tracking its own. */
    u32 rx_prbs_seq;

    /* Shared-state lock. */
    spinlock_t lock;
};

int  emu_gpu_init(struct emu_gpu_engine *eng, struct emu_bar0 *bar);
int  emu_gpu_start(struct emu_gpu_engine *eng);
void emu_gpu_stop(struct emu_gpu_engine *eng);
void emu_gpu_destroy(struct emu_gpu_engine *eng);

/* Address lookup contract: fake_dma values stored in the per-buffer
 * address registers map to kernel virtual addresses.  The definition
 * is provided by nvidia_p2p_stub; a __weak fallback returning NULL
 * allows the engine to load but take no real work. */
extern void *emu_gpu_addr_lookup(u64 fake_dma);

/* Drop the stub-module pin held by the poll thread's lazy symbol_get.
 * Called from emu_main.c:emu_on_stub_empty (drain callback) and as a
 * defensive fallback from emu_exit. Idempotent. */
void emu_gpu_drop_pin(void);

#endif /* __EMU_GPU_ENGINE_H__ */
