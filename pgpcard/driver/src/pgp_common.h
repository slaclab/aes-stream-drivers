/**
 *-----------------------------------------------------------------------------
 * Title      : Common PGP functions
 * ----------------------------------------------------------------------------
 * File       : pgp_common.h
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2016-08-08
 * Last update: 2016-08-08
 * ----------------------------------------------------------------------------
 * Description:
 * Common PGP functions
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
#ifndef __PGP_COMMON_H__
#define __PGP_COMMON_H__

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <PgpDriver.h>
#include <dma_common.h>

struct pgpprom_reg {
   uint32_t promData; 
   uint32_t promAddr; 
   uint32_t promRead; 
};

// Display card Info
void PgpCard_InfoShow(struct seq_file *s, struct PgpInfo *info);

// Display PCI Status
void PgpCard_PciShow(struct seq_file *s, struct PciStatus *status);

// Display Lane Status
void PgpCard_LaneShow(struct seq_file *s, struct PgpStatus *status);

// Prom Read 
int32_t PgpCard_PromWrite(struct DmaDevice *dev, struct pgpprom_reg *reg, uint64_t arg);

// Prom write 
int32_t PgpCard_PromRead(struct DmaDevice *dev, struct pgpprom_reg *reg, uint64_t arg);

#endif

