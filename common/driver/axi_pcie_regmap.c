/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description: Abstraction of the axi-pcie-core register mapping.
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
#include <axi_pcie_regmap.h>
#include <dma_common.h>
#include <axi_version.h>

struct reg_block {
   struct {
      uint32_t offset;
      uint32_t size;
   } regs[REG_COUNT];
};

/** Address map for device registers. */
#define AGEN2_OFF_V0       0x00000000
#define AGEN2_SIZE_V0      0x00010000
#define PHY_OFF_V0         0x00010000
#define PHY_SIZE_V0        0x00010000
#define AVER_OFF_V0        0x00020000
#define AVER_SIZE_V0       0x00010000
#define PROM_OFF_V0        0x00030000
#define PROM_SIZE_V0       0x00050000
#define USER_OFF_V0        0x00800000
#define USER_SIZE_V0       0x00800000
#define GPU_ASYNC_OFF_V0   0x00028000
#define GPU_ASYNC_SIZE_V0  0x00010000
/* AxiSysMon is present in "V0", but only after ~2022 or so (I dont know the exact axi-pcie-core version).
 * Thus, we'll pretend it's *not* present in V0 since it is not always there, depending on age. */
#define ASYSMON_OFF_V0  INVALID_REG_OFFSET
#define ASYSMON_SIZE_V0 0

#define ASYSMON_OFF_V1  0x00024000
#define ASYSMON_SIZE_V1 0x00004000

static const struct reg_block register_map[MAX_SUPPORTED_REG_VERSION+1] = {
   [0] = {  /* Version 0 */
      .regs = {
         [REG_AXIS_GEN2]      = {.offset = AGEN2_OFF_V0,       .size = AGEN2_SIZE_V0   },
         [REG_PHY]            = {.offset = PHY_OFF_V0,         .size = PHY_SIZE_V0     },
         [REG_AXI_VERSION]    = {.offset = AVER_OFF_V0,        .size = AVER_SIZE_V0    },
         [REG_AXI_SYSMON]     = {.offset = ASYSMON_OFF_V0,     .size = ASYSMON_SIZE_V0 },
         [REG_PROM]           = {.offset = PROM_OFF_V0,        .size = PROM_SIZE_V0    },
         [REG_USER]           = {.offset = USER_OFF_V0,        .size = USER_SIZE_V0    },
         [REG_GPU_ASYNC]      = {.offset = GPU_ASYNC_OFF_V0,   .size = GPU_ASYNC_SIZE_V0 }
      }
   },
   [1] = {  /* Version 1: Most offsets unchanged. */
      .regs = {
         [REG_AXIS_GEN2]      = {.offset = AGEN2_OFF_V0,       .size = AGEN2_SIZE_V0   },
         [REG_PHY]            = {.offset = PHY_OFF_V0,         .size = PHY_SIZE_V0     },
         [REG_AXI_VERSION]    = {.offset = AVER_OFF_V0,        .size = AVER_SIZE_V0    },
         [REG_AXI_SYSMON]     = {.offset = ASYSMON_OFF_V1,     .size = ASYSMON_SIZE_V1 },
         [REG_PROM]           = {.offset = PROM_OFF_V0,        .size = PROM_SIZE_V0    },
         [REG_USER]           = {.offset = USER_OFF_V0,        .size = USER_SIZE_V0    },
         [REG_GPU_ASYNC]      = {.offset = GPU_ASYNC_OFF_V0,   .size = GPU_ASYNC_SIZE_V0 }
      }
   }
};

/**
 * @brief Init the memory map for the device.
 * Unfortunately this is a bit of a chicken-and-the-egg problem, since we need to read the map
 * version from AxiVersion, which itself may be versioned by the mem map version.
 * We have to assume that AxiVersion will not move, if it does, the caller of this function is
 * responsible for giving us the right address :(
 */
int AxiRegMap_Init(struct DmaDevice* dev, __iomem void* axiVersion) {
   __iomem struct AxiVersion_Reg *reg = (__iomem struct AxiVersion_Reg *)axiVersion;
   dev->regMapVersion = readl(&reg->userValues[AxiVersion_Usr_MemoryMapVer_Offset]) & 0xFF;

   if (dev->regMapVersion > MAX_SUPPORTED_REG_VERSION) {
      dev_warn(dev->device, "AxiRegMap_Init: Unsupported register map version %u. Maximum supported version is %d.\n",
               dev->regMapVersion, MAX_SUPPORTED_REG_VERSION);
      dev_warn(dev->device, "Please use a newer driver version, or contact a maintainer to update the software\n");
      return -EPFNOSUPPORT;
   }
   dev_info(dev->device, "AxiRegMap_Init: Using register map version %d\n", dev->regMapVersion);
   return 0;
}

/**
 * @brief Query the offset of a register block within axi-pcie-core
 * @param dev The device.
 * @param block The block index
 * @returns Offset of the block, or INVALID_REG_OFFSET (UINT32_MAX) if invalid.
 */
uint32_t AxiRegMap_GetOffset(struct DmaDevice* dev, reg_block_index_t block) {
   if (dev->regMapVersion > MAX_SUPPORTED_REG_VERSION)
      return INVALID_REG_OFFSET;

   const struct reg_block* bl = &register_map[dev->regMapVersion];
   // This is pretty ugly, but if we forget to add an entry to register_map for the corresponding
   // block, offset/size gets set to 0 (global scope static storage, anyway). offset=0 is valid, but size=0 is not.
   if (bl->regs[block].size)
      return bl->regs[block].offset;
   return INVALID_REG_OFFSET;
}

/**
 * @brief Query the size of a register block within axi-pcie-core
 * @param dev The device.
 * @param block The block index.
 * @returns Size of the block, or 0 if invalid.
 */
uint32_t AxiRegMap_GetSize(struct DmaDevice* dev, reg_block_index_t block) {
   if (dev->regMapVersion > MAX_SUPPORTED_REG_VERSION)
      return 0;

   const struct reg_block* bl = &register_map[dev->regMapVersion];
   return bl->regs[block].size;
}
