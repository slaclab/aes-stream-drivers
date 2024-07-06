/**
 *-----------------------------------------------------------------------------
 * Title      : Top level module
 * ----------------------------------------------------------------------------
 * File       : rce_map.c
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2018-06-07
 * ----------------------------------------------------------------------------
 * Description:
 * Top level module types and functions.
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
#include <rce_map.h>
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
#define MOD_NAME "rce_memmap"

unsigned int cfgMinAddr = 0x80000000;
unsigned int cfgMaxAddr = 0xBFFFFFFF;

MODULE_AUTHOR("Ryan Herbst");
MODULE_DESCRIPTION("RCE Memory Map Interface");
MODULE_LICENSE("GPL");
module_init(Map_Init);
module_exit(Map_Exit);

// Global device
struct MapDevice dev;

// Global variable for the device class
struct class * gCl;

// Define interface routines
struct file_operations MapFunctions = {
   .read           = Map_Read,
   .write          = Map_Write,
   .open           = Map_Open,
   .release        = Map_Release,
   .unlocked_ioctl = (void *)Map_Ioctl,
   .compat_ioctl   = (void *)Map_Ioctl,
};

// Devnode callback to set permissions of created devices
char *Map_DevNode(struct device *dev, umode_t *mode) {
   if ( mode != NULL ) *mode = 0666;
   return(NULL);
}

// Create and init device
int Map_Init(void) {
   int32_t res;

   memset(&dev,0,sizeof(struct MapDevice));

   strcpy(dev.devName,MOD_NAME);//NOLINT

   // Allocate device numbers for character device. 1 minor numer starting at 0
   res = alloc_chrdev_region(&(dev.devNum), 0, 1, dev.devName);
   if (res < 0) {
      printk(KERN_ERR MOD_NAME " Init: Cannot register char device\n");
      return(-1);
   }

   // Create class struct if it does not already exist
   if (gCl == NULL) {
      printk(KERN_INFO MOD_NAME " Init: Creating device class\n");
      if ((gCl = class_create(THIS_MODULE, dev.devName)) == NULL) {
         printk(KERN_ERR MOD_NAME " Init: Failed to create device class\n");
         return(-1);
      }
      gCl->devnode = (void *)Map_DevNode;
   }

   // Attempt to create the device
   if (device_create(gCl, NULL, dev.devNum, NULL, dev.devName) == NULL) {
      printk(KERN_ERR MOD_NAME " Init: Failed to create device file\n");
      return -1;
   }

   // Init the device
   cdev_init(&(dev.charDev), &MapFunctions);
   dev.major = MAJOR(dev.devNum);

   // Add the charactor device
   if (cdev_add(&(dev.charDev), dev.devNum, 1) == -1) {
      printk(KERN_ERR MOD_NAME " Init: Failed to add device file.\n");
      return -1;
   }

   // Map initial space
   if ( (dev.maps = (struct MemMap *)kmalloc(sizeof(struct MemMap),GFP_KERNEL)) == NULL ) {
      printk(KERN_ERR MOD_NAME " Init: Could not allocate map memory\n");
      return (-1);
   }
   dev.maps->addr = cfgMinAddr;
   dev.maps->next = NULL;

   // Map space
   dev.maps->base = ioremap_wc(dev.maps->addr, MAP_SIZE);
   if (! dev.maps->base ) {
      printk(KERN_ERR MOD_NAME " Init: Could not map memory addr %p with size 0x%x.\n",(void *)dev.maps->addr,MAP_SIZE);
      kfree(dev.maps);
      return (-1);
   }
   printk(KERN_INFO MOD_NAME " Init: Mapped addr %p with size 0x%x to %p.\n",(void *)dev.maps->addr,MAP_SIZE,(void *)dev.maps->base);

   // Hold memory region
//   if ( request_mem_region(dev.maps->addr, MAP_SIZE, dev.devName) == NULL ) {
//      printk(KERN_ERR MOD_NAME " Map_Find: Memory in use.\n");
//      iounmap(dev.maps->base);
//      kfree(dev.maps);
//      return (-1);
//   }

   return(0);
}

// Cleanup device
void Map_Exit(void) {
   struct MemMap *tmp;

   // Unregister Device Driver
   if ( gCl != NULL ) device_destroy(gCl, dev.devNum);
   else printk(KERN_ERR MOD_NAME " Clean: gCl is already NULL.\n");

   unregister_chrdev_region(dev.devNum, 1);

   // Unmap
   while ( dev.maps != NULL ) {
      tmp = dev.maps;
      dev.maps = dev.maps->next;

      //release_mem_region(tmp->addr, MAP_SIZE);
      iounmap(tmp->base);
      kfree(tmp);
   }

   if (gCl != NULL) {
      printk(KERN_INFO MOD_NAME " Clean: Destroying device class\n");
      class_destroy(gCl);
      gCl = NULL;
   }
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
uint8_t * Map_Find(uint32_t addr) {
   struct MemMap *cur;
   struct MemMap *new;

   cur = dev.maps;

   if ( (addr < cfgMinAddr) || (addr > cfgMaxAddr) ) {
      printk(KERN_ERR MOD_NAME " Map_Find: Invalid address %p. Allowed range %p - %p\n",(void *)addr,(void *)cfgMinAddr,(void*)cfgMaxAddr);
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
            printk(KERN_ERR MOD_NAME " Map_Find: Could not allocate map memory\n");
            return NULL;
         }

         // Compute new base
         new->addr = (addr / MAP_SIZE) * MAP_SIZE;

         // Map space
         new->base = ioremap_wc(new->addr, MAP_SIZE);
         if (! new->base ) {
            printk(KERN_ERR MOD_NAME " Map_Find: Could not map memory addr %p (%p) with size 0x%x.\n",(void *)new->addr,(void*)addr,MAP_SIZE);
            kfree(new);
            return (NULL);
         }
         printk(KERN_INFO MOD_NAME " Map_Find: Mapped addr %p with size 0x%x to %p.\n",(void *)new->addr,MAP_SIZE,(void *)new->base);

         // Hold memory region
//         if ( request_mem_region(new->addr, MAP_SIZE, dev.devName) == NULL ) {
//            printk(KERN_ERR MOD_NAME " Map_Find: Memory in use.\n");
//            iounmap(cur->base);
//            kfree(new);
//            return (NULL);
//         }

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
            printk(KERN_WARNING MOD_NAME " Dma_Write_Register: copy_from_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &rData);
            return(-1);
         }

         if ( (base = Map_Find(rData.address)) == NULL ) return(-1);

         iowrite32(rData.data,base);
         return(0);
         break;

      // Register read
      case DMA_Read_Register:

         if ((ret=copy_from_user(&rData,(void *)arg,sizeof(struct DmaRegisterData)))) {
            printk(KERN_WARNING MOD_NAME " Dma_Read_Register: copy_from_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &rData);
            return(-1);
         }

         if ( (base = Map_Find(rData.address)) == NULL ) return(-1);
         rData.data = ioread32(base);

         // Return the data structure
         if ((ret=copy_to_user((void *)arg,&rData,sizeof(struct DmaRegisterData)))) {
            printk(KERN_WARNING MOD_NAME " Dma_Read_Register: copy_to_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &rData);
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

module_param(cfgMinAddr,uint,0);
MODULE_PARM_DESC(cfgMinAddr, "Min Map Addr");

module_param(cfgMaxAddr,uint,0);
MODULE_PARM_DESC(cfgMaxAddr, "Max Map Addr");

