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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
  /* 'ioremap_nocache' was deprecated in kernels >= 5.6, so instead we use 'ioremap' which
  is no-cache by default since kernels 2.6.25. */
#    define IOREMAP_NO_CACHE(address, size) ioremap(address, size)
#else /* KERNEL_VERSION < 2.6.25 */
#    define IOREMAP_NO_CACHE(address, size) ioremap_nocache(address, size)
#endif

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

// Devnode callback to set permissions of created devices
char *Map_DevNode(struct device *dev, umode_t *mode){
   if ( mode != NULL ) *mode = 0666;
   return(NULL);
}

// Create and init device
int Map_Init(void) {
   int32_t res;

   memset(&dev,0,sizeof(struct MapDevice));

   strcpy(dev.devName,MOD_NAME);

   // Allocate device numbers for character device. 1 minor numer starting at 0
   res = alloc_chrdev_region(&(dev.devNum), 0, 1, dev.devName);
   if (res < 0) {
      pr_err("%s: Init: Cannot register char device\n",MOD_NAME);
      return(-1);
   }

   // Create class struct if it does not already exist
   pr_info("%s: Init: Creating device class\n",MOD_NAME);
   if ((gCl = class_create(THIS_MODULE, dev.devName)) == NULL) {
      pr_err("%s: Init: Failed to create device class\n",MOD_NAME);
      unregister_chrdev_region(dev.devNum, 1); // Unregister device numbers on failure
      return(-1);
   }
   gCl->devnode = (void *)Map_DevNode;

   // Attempt to create the device
   if (device_create(gCl, NULL, dev.devNum, NULL, dev.devName) == NULL) {
      pr_err("%s: Init: Failed to create device file\n",MOD_NAME);
      class_destroy(gCl); // Destroy device class on failure
      unregister_chrdev_region(dev.devNum, 1); // Unregister device numbers on failure
      return -1;
   }

   // Init the device
   cdev_init(&(dev.charDev), &MapFunctions);
   dev.major = MAJOR(dev.devNum);

   // Add the charactor device
   if (cdev_add(&(dev.charDev), dev.devNum, 1) == -1) {
      pr_err("%s: Init: Failed to add device file.\n",MOD_NAME);
      device_destroy(gCl, dev.devNum); // Destroy device on failure
      class_destroy(gCl); // Destroy device class on failure
      unregister_chrdev_region(dev.devNum, 1); // Unregister device numbers on failure
      return -1;
   }

   // Map initial space
   if ( (dev.maps = (struct MemMap *)kmalloc(sizeof(struct MemMap),GFP_KERNEL)) == NULL ) {
      pr_err("%s: Init: Could not allocate map memory\n",MOD_NAME);
      cdev_del(&(dev.charDev)); // Remove character device on failure
      device_destroy(gCl, dev.devNum); // Destroy device on failure
      class_destroy(gCl); // Destroy device class on failure
      unregister_chrdev_region(dev.devNum, 1); // Unregister device numbers on failure
      return (-1);
   }
   dev.maps->addr = plMinAddr;
   dev.maps->next = NULL;

   // Map space
   dev.maps->base = IOREMAP_NO_CACHE(dev.maps->addr, MAP_SIZE);
   if (! dev.maps->base ) {
      pr_err("%s: Init: Could not map memory addr 0x%llx with size 0x%x.\n",MOD_NAME,(uint64_t)dev.maps->addr,MAP_SIZE);
      kfree(dev.maps);
      cdev_del(&(dev.charDev)); // Remove character device on failure
      device_destroy(gCl, dev.devNum); // Destroy device on failure
      class_destroy(gCl); // Destroy device class on failure
      unregister_chrdev_region(dev.devNum, 1); // Unregister device numbers on failure
      return (-1);
   }
   pr_info("%s: Init: Mapped addr 0x%llx with size 0x%x to 0x%llx.\n",MOD_NAME,(uint64_t)dev.maps->addr,MAP_SIZE,(uint64_t)dev.maps->base);

   return(0);
}

// Cleanup device
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

   pr_info("%s: Clean: Module unloaded successfully.\n",MOD_NAME);
}


// Open Returns 0 on success, error code on failure
int Map_Open(struct inode *inode, struct file *filp) {
   return 0;
}

// Close
int Map_Release(struct inode *inode, struct file *filp) {
   return 0;
}

// Find or allocate map space
uint8_t * Map_Find(uint64_t addr) {

   struct MemMap *cur;
   struct MemMap *new;

   cur = dev.maps;

   if ( ( (addr<psMinAddr) || (addr>psMaxAddr) ) && ( (addr<plMinAddr) || (addr>plMaxAddr) ) ) {
      pr_err("%s: Map_Find: Invalid address 0x%llx\n\tPS Allowed range 0x%llx - 0x%llx\n\tPL Allowed range 0x%llx - 0x%llx \n",MOD_NAME,(uint64_t)addr,(uint64_t)psMinAddr,(uint64_t)psMaxAddr,(uint64_t)plMinAddr,(uint64_t)plMaxAddr);
      return (NULL);
   }

   while (cur != NULL) {

      // Current pointer matches
      if ( (addr >= cur->addr) && (addr < (cur->addr + MAP_SIZE)) )
         return((uint8_t*)(cur->base + (addr-cur->addr)));

      // Next address is too high, insert new structure
      if ( (cur->next == NULL) || (addr < ((struct MemMap *)cur->next)->addr) ) {

         // Create new map
         if ( (new = (struct MemMap *)kmalloc(sizeof(struct MemMap),GFP_KERNEL)) == NULL ) {
            pr_err("%s: Map_Find: Could not allocate map memory\n",MOD_NAME);
            return NULL;
         }

         // Compute new base
         new->addr = (addr / MAP_SIZE) * MAP_SIZE;

         // Map space
         new->base = IOREMAP_NO_CACHE(new->addr, MAP_SIZE);
         if (! new->base ) {
            pr_err("%s: Map_Find: Could not map memory addr 0x%llx (0x%llx) with size 0x%x.\n",MOD_NAME,(uint64_t)new->addr,(uint64_t)addr,MAP_SIZE);
            kfree(new);
            return (NULL);
         }
         pr_info("%s: Map_Find: Mapped addr 0x%llx with size 0x%x to 0x%llx.\n",MOD_NAME,(uint64_t)new->addr,MAP_SIZE,(uint64_t)new->base);

         // Insert into list
         new->next = cur->next;
         cur->next = new;
      }
      cur = cur->next;

   }

   return(NULL);
}

// Perform commands
ssize_t Map_Ioctl(struct file *filp, uint32_t cmd, unsigned long arg) {
   struct DmaRegisterData rData;
   uint8_t *base;
   ssize_t ret;

   // Determine command
   switch (cmd) {

      // Get API Version
      case DMA_Get_Version:
         return(DMA_VERSION);
         break;

      // Register write
      case DMA_Write_Register:

         if ((ret = copy_from_user(&rData,(void *)arg,sizeof(struct DmaRegisterData)))) {
            pr_warn("%s: Dma_Write_Register: copy_from_user failed. ret=%i, user=%p kern=%p\n",MOD_NAME, ret, (void *)arg, &rData);
            return(-1);
         }

         if ( (base = Map_Find(rData.address)) == NULL ) return(-1);

         iowrite32(rData.data,base);
         return(0);
         break;

      // Register read
      case DMA_Read_Register:

         if ((ret=copy_from_user(&rData,(void *)arg,sizeof(struct DmaRegisterData)))) {
            pr_warn("%s: Dma_Read_Register: copy_from_user failed. ret=%i, user=%p kern=%p\n",MOD_NAME, ret, (void *)arg, &rData);
            return(-1);
         }

         if ( (base = Map_Find(rData.address)) == NULL ) return(-1);
         rData.data = ioread32(base);

         // Return the data structure
         if ((ret=copy_to_user((void *)arg,&rData,sizeof(struct DmaRegisterData)))) {
            pr_warn("%s: Dma_Read_Register: copy_to_user failed. ret=%i, user=%p kern=%p\n",MOD_NAME, ret, (void *)arg, &rData);
            return(-1);
         }
         return(0);
         break;

      default:
         break;
   }
   return(-1);
}

// Read not supported
ssize_t Map_Read(struct file *filp, char *buffer, size_t count, loff_t *f_pos) {
   return -1;
}

// Dma_Write
ssize_t Map_Write(struct file *filp, const char* buffer, size_t count, loff_t* f_pos) {
   return -1;
}

module_param(psMinAddr,ulong,0);
MODULE_PARM_DESC(psMinAddr, "PS Min Map Addr");

module_param(psMaxAddr,ulong,0);
MODULE_PARM_DESC(psMaxAddr, "PS Max Map Addr");

module_param(plMinAddr,ulong,0);
MODULE_PARM_DESC(plMinAddr, "PL Min Map Addr");

module_param(plMaxAddr,ulong,0);
MODULE_PARM_DESC(plMaxAddr, "PL Max Map Addr");
