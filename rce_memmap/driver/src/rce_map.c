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

// Module Name
#define MOD_NAME "rce_map"

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
      dev_err(dev.device,"Init: Cannot register char device\n");
      return(-1);
   }

   // Create class struct if it does not already exist
   if (gCl == NULL) {
      dev_info(dev.device,"Init: Creating device class\n");
      if ((gCl = class_create(THIS_MODULE, dev.devName)) == NULL) {
         dev_err(dev.device,"Init: Failed to create device class\n");
         return(-1);
      }
      gCl->devnode = (void *)Map_DevNode;
   }

   // Attempt to create the device
   if (device_create(gCl, NULL, dev.devNum, NULL, dev.devName) == NULL) {
      dev_err(dev.device,"Init: Failed to create device file\n");
      return -1;
   }

   // Init the device
   cdev_init(&(dev.charDev), &MapFunctions);
   dev.major = MAJOR(dev.devNum);

   // Add the charactor device
   if (cdev_add(&(dev.charDev), dev.devNum, 1) == -1) {
      dev_err(dev.device,"Init: Failed to add device file.\n");
      return -1;
   }                                  

   dev.baseAddr = MAP_BASE;
   dev.baseSize = MAP_SIZE;

   dev_info(dev.device,"Init: Mapping Register space %p with size 0x%x.\n",(void *)dev.baseAddr,dev.baseSize);
   dev.base = ioremap_nocache(dev.baseAddr, dev.baseSize);
   if (! dev.base ) {
      dev_err(dev.device,"Init: Could not remap memory.\n");
      return -1;
   }
   dev_info(dev.device,"Init: Mapped to %p.\n",dev.base);

   // Hold memory region
   if ( request_mem_region(dev.baseAddr, dev.baseSize, dev.devName) == NULL ) {
      dev_err(dev.device,"Init: Memory in use.\n");
      return -1;
   }

   return(0);
}

// Cleanup device
void Map_Exit(void) {

   // Unregister Device Driver
   if ( gCl != NULL ) device_destroy(gCl, dev.devNum);
   else dev_warn(dev.device,"Clean: gCl is already NULL.\n");

   unregister_chrdev_region(dev.devNum, 1);

   // Release memory region
   release_mem_region(dev.baseAddr, dev.baseSize);

   // Unmap
   iounmap(dev.base);

   if (gCl != NULL) {
      dev_info(dev.device,"Clean: Destroying device class\n");
      class_destroy(gCl);
      gCl = NULL;
   }
}

// Open Returns 0 on success, error code on failure
int Map_Open(struct inode *inode, struct file *filp) {
   struct MapDevice * dev;

   // Find device structure
   dev = container_of(inode->i_cdev, struct MapDevice, charDev);

   // Store for later
   filp->private_data = dev;
   return 0;
}

// Close
int Map_Release(struct inode *inode, struct file *filp) {
   return 0;
}

// Perform commands
ssize_t Map_Ioctl(struct file *filp, uint32_t cmd, unsigned long arg) {
   struct MapDevice * dev;
   struct DmaRegisterData rData;
   ssize_t ret;

   dev = (struct MapDevice *)filp->private_data;

   // Determine command
   switch (cmd) {

      // Get API Version
      case DMA_Get_Version:
         return(DMA_VERSION);
         break;

      // Register write
      case DMA_Write_Register:

         if ((ret = copy_from_user(&rData,(void *)arg,sizeof(struct DmaRegisterData)))) {
            dev_warn(dev->device,"Dma_Write_Register: copy_from_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &rData);
            return(-1);
         }

         if ( (rData.address < dev->baseAddr) || ((rData.address + 4) > (dev->baseAddr + dev->baseSize)) ) return (-1);

         iowrite32(rData.data,dev->base+(rData.address-dev->baseAddr));
         return(0);
         break;

      // Register read
      case DMA_Read_Register:

         if ((ret=copy_from_user(&rData,(void *)arg,sizeof(struct DmaRegisterData)))) {
            dev_warn(dev->device,"Dma_Read_Register: copy_from_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &rData);
            return(-1);
         }

         rData.data = ioread32(dev->base+(rData.address-dev->baseAddr));

         // Return the data structure
         if ((ret=copy_to_user((void *)arg,&rData,sizeof(struct DmaRegisterData)))) {
            dev_warn(dev->device,"Dma_Read_Register: copy_to_user failed. ret=%i, user=%p kern=%p\n", ret, (void *)arg, &rData);
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

