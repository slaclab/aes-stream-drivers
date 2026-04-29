/**
 *----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *----------------------------------------------------------------------------
 * Description:
 *    Private inter-TU declarations for emu_gpu_addr_table.c. Include this
 *    in nvidia_p2p_stub.c to access the internal API that is NOT
 *    EXPORT_SYMBOL'd. The three cross-module EXPORT_SYMBOL_GPL symbols are
 *    declared in the public emu_gpu_addr_table.h.
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
#ifndef __EMU_GPU_ADDR_TABLE_PRIV_H__
#define __EMU_GPU_ADDR_TABLE_PRIV_H__

#include "emu_gpu_addr_table.h"

struct vm_area_struct;   /* forward decl for emu_gpu_addr_is_stub_vma */

/* Private inter-TU API (used by nvidia_p2p_stub.c; not EXPORTed). */
int  emu_gpu_addr_table_init(void);
void emu_gpu_addr_table_exit(void);
int  emu_gpu_addr_table_alloc_and_register(u64 length,
                                           struct emu_gpu_addr_entry **out);
void emu_gpu_addr_table_get(struct emu_gpu_addr_entry *entry);
void emu_gpu_addr_table_release(struct emu_gpu_addr_entry *entry);
struct emu_gpu_addr_entry *emu_gpu_addr_table_find_by_idx(u32 idx);
bool emu_gpu_addr_is_stub_vma(const struct vm_area_struct *vma);

#endif /* __EMU_GPU_ADDR_TABLE_PRIV_H__ */
