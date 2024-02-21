/**
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
#include <aximemorymap.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/version.h>

// Module Name
#define MOD_NAME "axi_memory_map"

unsigned long psMinAddr = 0xFF000000; // PS peripherals (SPI, I2C, etc)
unsigned long psMaxAddr = 0xFFFFFFFF; // PS peripherals (SPI, I2C, etc)

unsigned long plMinAddr = 0x400000000; // Edit this to match your PL AXI port address configurations
unsigned long plMaxAddr = 0x4FFFFFFFF; // Edit this to match your PL AXI port address configurations

MODULE_AUTHOR("Ryan Herbst");
MODULE_DESCRIPTION("AXI Memory Map Interface");
MODULE_LICENSE("GPL");
module_init(Map_Init);
module_exit(Map_Exit);

// Global device
struct MapDevice dev;

// Global variable for the device class
struct class * gCl = NULL;

// Define interface routines
struct file_operations MapFunctions = {
   read:           Map_Read,
   write:          Map_Write,
   open:           Map_Open,
   release:        Map_Release,
   unlocked_ioctl: (void *)Map_Ioctl,
   compat_ioctl:   (void *)Map_Ioctl,
};

/**
 * Map_DevNode - Devnode callback to dynamically set device file permissions
 * @dev: The device structure
 * @mode: Pointer to the mode (permission bits) to be set for the device file
 *
 * This callback function is invoked when the device class is created and allows
 * setting of the device file permissions dynamically. By default, it sets the
 * device permissions to be globally readable and writable (0666). This behavior
 * can be customized based on security requirements or user preferences.
 *
 * Note: The permissions set by this callback can be overridden by udev rules on
 * systems where udev is responsible for device node creation and management.
 */
char *Map_DevNode(struct device *dev, umode_t *mode) {
   if (mode != NULL) *mode = 0666; // Set default permissions to read and write for user, group, and others
   return NULL; // Return NULL as no specific device node name alteration is required
}

/**
 * Map_Init - Initialize the AXI memory map device
 *
 * This function sets up the AXI memory map device. It involves several steps:
 * 1. Zeroing out the device structure to ensure a clean start.
 * 2. Setting the device name from the predefined module name.
 * 3. Dynamically allocating a device number for the device.
 * 4. Creating a device class that will be used to create device files.
 * 5. Creating a device file for the device class.
 * 6. Initializing the character device with the device's file operations.
 * 7. Adding the character device to the system, making it active.
 * 8. Allocating an initial memory map for the device to use.
 * 9. Mapping the initial memory space for the device's operations.
 *
 * Each step checks for errors, and if an error occurs, the function will
 * clean up any allocated resources and return an error code.
 *
 * Return: 0 on success, negative error code on failure.
 */
int Map_Init(void) {
   int32_t res;

   // Step 1: Zero out the device structure
   memset(&dev, 0, sizeof(struct MapDevice));

   // Step 2: Set the device name
   if (strscpy(dev.devName, MOD_NAME, sizeof(dev.devName)) < 0) {
      pr_err("%s: Init: Source string too long for destination\n", MOD_NAME);
      return -1;
   }

   // Step 3: Allocate device numbers dynamically
   res = alloc_chrdev_region(&dev.devNum, 0, 1, dev.devName);
   if (res < 0) {
      pr_err("%s: Init: Cannot register char device\n", MOD_NAME);
      return -1;
   }

   // Step 4: Create a device class
   pr_info("%s: Init: Creating device class\n", MOD_NAME);
   gCl = class_create(THIS_MODULE, dev.devName);
   if (IS_ERR(gCl)) {
      pr_err("%s: Init: Failed to create device class\n", MOD_NAME);
      unregister_chrdev_region(dev.devNum, 1); // Clean up allocated resources
      return PTR_ERR(gCl);
   }
   gCl->devnode = Map_DevNode;

   // Step 5: Create a device file
   if (device_create(gCl, NULL, dev.devNum, NULL, dev.devName) == NULL) {
      pr_err("%s: Init: Failed to create device file\n", MOD_NAME);
      class_destroy(gCl); // Clean up on failure
      unregister_chrdev_region(dev.devNum, 1);
      return -1;
   }

   // Step 6: Initialize the character device
   cdev_init(&dev.charDev, &MapFunctions);
   dev.major = MAJOR(dev.devNum);

   // Step 7: Add the character device
   if (cdev_add(&dev.charDev, dev.devNum, 1) == -1) {
      pr_err("%s: Init: Failed to add device file.\n", MOD_NAME);
      device_destroy(gCl, dev.devNum); // Clean up on failure
      class_destroy(gCl);
      unregister_chrdev_region(dev.devNum, 1);
      return -1;
   }

   // Step 8: Allocate initial memory map
   dev.maps = (struct MemMap *)kzalloc(sizeof(struct MemMap), GFP_KERNEL);
   if (dev.maps == NULL) {
      pr_err("%s: Init: Could not allocate map memory\n", MOD_NAME);
      cdev_del(&dev.charDev); // Clean up on failure
      device_destroy(gCl, dev.devNum);
      class_destroy(gCl);
      unregister_chrdev_region(dev.devNum, 1);
      return -1;
   }
   dev.maps->addr = plMinAddr;
   dev.maps->next = NULL;

   // Step 9: Map initial memory space
   dev.maps->base = ioremap_wc(dev.maps->addr, MAP_SIZE);
   if (!dev.maps->base) {
      pr_err("%s: Init: Could not map memory addr 0x%llx with size 0x%x.\n", MOD_NAME, (uint64_t)dev.maps->addr, MAP_SIZE);
      kfree(dev.maps); // Clean up on failure
      cdev_del(&dev.charDev);
      device_destroy(gCl, dev.devNum);
      class_destroy(gCl);
      unregister_chrdev_region(dev.devNum, 1);
      return -1;
   }
   pr_info("%s: Init: Mapped addr 0x%llx with size 0x%x to 0x%llx.\n", MOD_NAME, (uint64_t)dev.maps->addr, MAP_SIZE, (uint64_t)dev.maps->base);

   return 0;
}

/**
 * Map_Exit - Cleanup function for AXI memory map device
 *
 * This function is responsible for cleaning up resources allocated during the
 * operation of the device. It performs the following operations:
 * - Unregisters the device driver, releasing the device number.
 * - Iterates through the linked list of memory maps, unmaps each memory region
 *   from the device's address space, and frees the associated memory.
 * - Destroys the device class if it has been created.
 * - Logs the successful cleanup of the module.
 */
void Map_Exit(void) {
   struct MemMap *tmp, *next;

   // Unregister Device Driver
   unregister_chrdev_region(dev.devNum, 1);

   // Unmap and release allocated memory
   tmp = dev.maps;
   while (tmp != NULL) {
      next = tmp->next;
      iounmap(tmp->base);
      kfree(tmp);
      tmp = next;
   }

   // Destroy the device class
   if (gCl != NULL) {
      class_destroy(gCl);
      gCl = NULL;
   }

   pr_info("%s: Clean: Module unloaded successfully.\n", MOD_NAME);
}

/**
 * Map_Open - Open operation for AXI memory map device
 * @inode: inode structure representing the device file
 * @filp: file pointer to the opened device file
 *
 * This function is called when the AXI memory map device file is opened.
 * Currently, it doesn't perform any specific initialization or resource allocation,
 * as the device does not require any special setup upon opening. However, this
 * function can be expanded in the future to include such operations if needed.
 *
 * Return: Always returns 0, indicating success.
 */
int Map_Open(struct inode *inode, struct file *filp) {
   // Placeholder for any future open operation code
   return 0;
}

/**
 * Map_Release - Release the device
 * @inode: pointer to the inode structure
 * @filp: pointer to the file structure
 *
 * This function is called when the last reference to an open file is closed.
 * It's a placeholder for releasing any resources allocated during the open operation.
 * Currently, this driver does not allocate resources per open, so there's no
 * specific release logic needed. However, this function is required to comply with
 * the file_operations structure.
 *
 * Return: 0 on success. Currently, always returns 0 as there's no failure scenario
 *         implemented.
 */
int Map_Release(struct inode *inode, struct file *filp) {
   // Placeholder for any cleanup operations. Currently, no specific action required.

   // Always return 0 indicating success.
   return 0;
}

/**
 * Map_Find - Locate or allocate memory map space for a given address
 * @addr: The address for which to find or allocate map space
 *
 * This function searches the existing memory maps for the address. If the address
 * is within the allowed ranges and not already mapped, it allocates a new memory map.
 * It ensures the address falls within predefined ranges before attempting to map,
 * returning NULL for addresses outside these ranges or on allocation failure.
 *
 * Return: Pointer to the mapped address space on success, NULL on failure.
 */
uint8_t *Map_Find(uint64_t addr) {
   struct MemMap *cur;
   struct MemMap *new;

   cur = dev.maps;

   // Validate address range
   if (((addr < psMinAddr) || (addr > psMaxAddr)) &&
       ((addr < plMinAddr) || (addr > plMaxAddr))) {
      pr_err("%s: Map_Find: Invalid address 0x%llx\n\tPS Allowed range 0x%llx - 0x%llx\n\tPL Allowed range 0x%llx - 0x%llx\n",
             MOD_NAME, (uint64_t)addr, (uint64_t)psMinAddr, (uint64_t)psMaxAddr,
             (uint64_t)plMinAddr, (uint64_t)plMaxAddr);
      return (NULL);
   }

   // Search for existing map or allocate new
   while (cur != NULL) {
      // If current map contains address, return offset within map
      if ((addr >= cur->addr) && (addr < (cur->addr + MAP_SIZE)))
         return ((uint8_t *)(cur->base + (addr - cur->addr)));

      // If address is beyond current map, and no next map or next map is further, insert new
      if ((cur->next == NULL) || (addr < ((struct MemMap *)cur->next)->addr)) {
         // Allocate and initialize new map structure
         if ((new = (struct MemMap *)kzalloc(sizeof(struct MemMap), GFP_KERNEL)) == NULL) {
            pr_err("%s: Map_Find: Could not allocate map memory\n", MOD_NAME);
            return NULL;
         }

         new->addr = (addr / MAP_SIZE) * MAP_SIZE; // Align to MAP_SIZE
         new->base = ioremap_wc(new->addr, MAP_SIZE); // Map physical address
         if (!new->base) {
            pr_err("%s: Map_Find: Could not map memory addr 0x%llx (0x%llx) with size 0x%x.\n",
                   MOD_NAME, (uint64_t)new->addr, (uint64_t)addr, MAP_SIZE);
            kfree(new);
            return (NULL);
         }
         pr_info("%s: Map_Find: Mapped addr 0x%llx with size 0x%x to 0x%llx.\n",
                 MOD_NAME, (uint64_t)new->addr, MAP_SIZE, (uint64_t)new->base);

         // Insert new map into list
         new->next = cur->next;
         cur->next = new;
      }
      cur = cur->next;
   }

   return (NULL);
}

/**
 * Map_Ioctl - Handle IOCTL commands for the AXI memory map device
 * @filp: File pointer to the device file
 * @cmd: IOCTL command code
 * @arg: Argument to the IOCTL command
 *
 * This function supports various IOCTL commands for interacting with
 * the AXI memory map device, such as reading and writing to DMA registers.
 * Each command is handled based on its case, with appropriate actions
 * including copying data to/from user space and performing device operations.
 *
 * Return: On success, returns 0 or positive value. On error, returns a negative value.
 */
ssize_t Map_Ioctl(struct file *filp, uint32_t cmd, unsigned long arg) {
   struct DmaRegisterData rData;
   uint8_t *base;
   ssize_t ret = -1; // Default return value for error

   // Determine which IOCTL command is being executed
   switch (cmd) {
      case DMA_Get_Version:
         // Return the current version of the DMA API
         ret = DMA_VERSION;
         break;

      case DMA_Write_Register:
         // Get register data from user space
         if (!get_user(rData.address, &((struct DmaRegisterData __user *)arg)->address) &&
             !get_user(rData.data, &((struct DmaRegisterData __user *)arg)->data)) {
            // Find the memory base address for the register
            if ((base = Map_Find(rData.address)) != NULL) {
               // Write data to the register
               writel(rData.data, base);
               ret = 0; // Success
            } else {
               pr_warn("%s: Dma_Write_Register: Map_Find failed.\n", MOD_NAME);
            }
         } else {
            pr_warn("%s: Dma_Write_Register: get_user failed.\n", MOD_NAME);
         }
         break;

      case DMA_Read_Register:
         // Get register address from user space
         if (!get_user(rData.address, &((struct DmaRegisterData __user *)arg)->address)) {
            // Find the memory base address for the register
            if ((base = Map_Find(rData.address)) != NULL) {
               // Read data from the register
               rData.data = readl(base);
               // Put the updated register data back to user space
               if (!put_user(rData.data, &((struct DmaRegisterData __user *)arg)->data)) {
                  ret = 0; // Success
               } else {
                  pr_warn("%s: Dma_Read_Register: put_user failed.\n", MOD_NAME);
               }
            } else {
               pr_warn("%s: Dma_Read_Register: Map_Find failed.\n", MOD_NAME);
            }
         } else {
            pr_warn("%s: Dma_Read_Register: get_user failed.\n", MOD_NAME);
         }
         break;

      default:
         // Unsupported IOCTL command
         pr_warn("%s: Map_Ioctl: Unsupported IOCTL command.\n", MOD_NAME);
   }

   return ret; // Return the result
}

/**
 * Map_Read - Read operation for AXI memory map device
 * @filp: file pointer to the opened device file
 * @buffer: user space buffer to read the data into
 * @count: number of bytes to read
 * @f_pos: offset in the file
 *
 * This function is called when a read operation is performed on the AXI memory map device file.
 * In the current implementation, reading directly from the device is not supported. This could be
 * due to the nature of the device or specific security or operational considerations.
 *
 * The function returns an error code to indicate that the operation is not permitted. Future
 * implementations could modify this behavior to support read operations, depending on the
 * requirements and capabilities of the hardware.
 *
 * Return: Returns -1 to indicate that the read operation is not supported.
 */
ssize_t Map_Read(struct file *filp, char *buffer, size_t count, loff_t *f_pos) {
   return -1;  // DMA read operation is not supported
}

/**
 * Map_Write - Write operation for AXI memory map device
 * @filp: File pointer to the opened device file
 * @buffer: User space buffer containing the data to write
 * @count: Number of bytes to write
 * @f_pos: Offset into the device
 *
 * This function is called when a user space application attempts to write
 * data to the AXI memory map device. It translates the user space buffer
 * address to a kernel space address, finds the appropriate memory mapped
 * region, and writes the provided data into this memory region.
 *
 * Currently, this function is not implemented and simply returns -1. To
 * support write operations, one would need to validate input parameters,
 * copy data from user space to kernel space, and perform the necessary
 * memory writes to the mapped device.
 *
 * Return: On success, the number of bytes written. On error, a negative value.
 */
ssize_t Map_Write(struct file *filp, const char* buffer, size_t count, loff_t* f_pos) {
   return -1; // DMA write operation is not supported
}

module_param(psMinAddr,ulong,0);
MODULE_PARM_DESC(psMinAddr, "PS Min Map Addr");

module_param(psMaxAddr,ulong,0);
MODULE_PARM_DESC(psMaxAddr, "PS Max Map Addr");

module_param(plMinAddr,ulong,0);
MODULE_PARM_DESC(plMinAddr, "PL Min Map Addr");

module_param(plMaxAddr,ulong,0);
MODULE_PARM_DESC(plMaxAddr, "PL Max Map Addr");
