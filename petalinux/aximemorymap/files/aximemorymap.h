/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    Defines an interface for the AXI Memory Map Kernel Driver, facilitating
 *    device-agnostic memory mapping and access in Linux kernel space. This header
 *    file outlines the data structures and function prototypes used to manipulate
 *    memory mappings, enabling efficient, low-level interaction with hardware
 *    through a uniform API. It is designed for use within the aes_stream_drivers
 *    package, ensuring compatibility with a wide range of devices.
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

#ifndef __AXI_MEMORY_MAP_H__
#define __AXI_MEMORY_MAP_H__

#include <linux/types.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <DmaDriver.h>

// Defines the size of the map, set to 64K.
#define MAP_SIZE 0x10000

/**
 * struct MemMap - Represents a single memory mapping.
 * @addr: Physical address of the mapping.
 * @base: Virtual base address of the mapped memory.
 * @next: Pointer to the next memory map structure.
 *
 * This structure is used to keep track of individual memory mappings.
 */
struct MemMap {
   uint64_t addr;
   uint8_t *base;
   void *next;
};

/**
 * struct MapDevice - Device structure for memory mapping.
 * @major: Major number assigned to the device.
 * @devNum: Device number.
 * @devName: Name of the device.
 * @charDev: Character device structure.
 * @device: Device structure.
 * @maps: Pointer to the first memory map structure.
 *
 * This structure represents a memory mapping device, including its
 * character device representation and list of memory mappings.
 */
struct MapDevice {
   uint32_t major;
   dev_t devNum;
   char devName[50];
   struct cdev charDev;
   struct device *device;
   struct MemMap *maps;
};

// Function prototypes for device operations.
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
char *Map_DevNode(struct device *dev, umode_t *mode);
#else
char *Map_DevNode(const struct device *dev, umode_t *mode);
#endif
int Map_Init(void);
void Map_Exit(void);
int Map_Open(struct inode *inode, struct file *filp);
int Map_Release(struct inode *inode, struct file *filp);
ssize_t Map_Read(struct file *filp, char *buffer, size_t count, loff_t *f_pos);
ssize_t Map_Write(struct file *filp, const char *buffer, size_t count, loff_t *f_pos);
uint8_t *Map_Find(uint64_t addr);
ssize_t Map_Ioctl(struct file *filp, uint32_t cmd, unsigned long arg);

#endif // __AXI_MEMORY_MAP_H__
