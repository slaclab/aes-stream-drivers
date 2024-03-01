/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 *    Implements the driver functionality for reading and managing
 *    the AXI version register within a hardware device. It provides an interface for accessing
 *    version information, facilitating compatibility checks and firmware updates. The code includes
 *    routines to read the version register, interpret the version data, and report it back to the
 *    user or application, ensuring smooth integration and operation within a larger system.
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

#include <axi_version.h>
#include <AxiVersion.h>
#include <dma_common.h>
#include <linux/seq_file.h>

/**
 * AxiVersion_Get - Retrieve the AXI version information.
 * @dev: Pointer to the device structure.
 * @base: Base address of the device.
 * @arg: User-space pointer to store the AXI version information.
 *
 * This function reads the AXI version from the device and copies it to
 * user space. It uses the AxiVersion_Read function to obtain the version
 * information from the device registers and then copies this information
 * to a user-space location specified by @arg.
 *
 * Return: 0 on success, -1 on failure to copy data to user space.
 */
int32_t AxiVersion_Get(struct DmaDevice *dev, void *base, uint64_t arg) {
   struct AxiVersion axiVersion;
   int32_t ret;

   // Read the AXI version from the device
   AxiVersion_Read(dev, base, &axiVersion);

   // Attempt to copy the AXI version information to user space
   ret = copy_to_user((void *)arg, &axiVersion, sizeof(struct AxiVersion));
   if (ret) {
      // Log a warning if copy to user space fails
      dev_warn(dev->device,
               "AxiVersion_Get: copy_to_user failed. ret=%i, user=%p kern=%p\n",
               ret, (void *)arg, &axiVersion);
      return -1;
   }
   return 0;
}

/**
 * AxiVersion_Read - Reads the AXI version information.
 * @dev: Pointer to the DmaDevice structure.
 * @base: Base address of the AXI version registers.
 * @aVer: Pointer to the AxiVersion structure to populate.
 *
 * This function reads the AXI version information from the hardware registers
 * and populates the provided AxiVersion structure with the firmware version,
 * scratch pad, up time count, feature descriptor value, user-defined values,
 * device ID, git hash, device DNA value, and build string.
 */
void AxiVersion_Read(struct DmaDevice *dev, void * base, struct AxiVersion *aVer) {
   struct AxiVersion_Reg *reg = (struct AxiVersion_Reg *)base;
   uint32_t x;

   // Read firmware version, scratch pad, and uptime count
   aVer->firmwareVersion = readl(&(reg->firmwareVersion));
   aVer->scratchPad      = readl(&(reg->scratchPad));
   aVer->upTimeCount     = readl(&(reg->upTimeCount));

   // Read feature descriptor values
   for (x = 0; x < 2; x++) {
      ((uint32_t *)aVer->fdValue)[x] = readl(&(reg->fdValue[x]));
   }

   // Read user-defined values
   for (x = 0; x < 64; x++) {
      aVer->userValues[x] = readl(&(reg->userValues[x]));
   }

   // Read device ID
   aVer->deviceId = readl(&(reg->deviceId));

   // Read git hash
   for (x = 0; x < 40; x++) {
      ((uint32_t *)aVer->gitHash)[x] = readl(&(reg->gitHash[x]));
   }

   // Read device DNA value
   for (x = 0; x < 4; x++) {
      ((uint32_t *)aVer->dnaValue)[x] = readl(&(reg->dnaValue[x]));
   }

   // Read build string
   for (x = 0; x < 64; x++) {
      ((uint32_t *)aVer->buildString)[x] = readl(&(reg->buildString[x]));
   }
}

/**
 * AxiVersion_Show - Display AXI version information.
 * @s: sequence file pointer to which this function will write.
 * @dev: pointer to the DmaDevice structure.
 * @aVer: pointer to the AxiVersion structure containing version info.
 *
 * This function prints the firmware version information, including firmware
 * version, scratch pad, up time count, Git hash, DNA value, and build string
 * of the AXI to the provided sequence file. The function checks if the Git hash
 * indicates uncommitted code (marked as 'dirty') and formats the output
 * accordingly.
 */
void AxiVersion_Show(struct seq_file *s, struct DmaDevice *dev, struct AxiVersion *aVer) {
   int32_t x;
   bool gitDirty = true;

   seq_printf(s,"---------- Firmware Axi Version -----------\n");
   seq_printf(s,"     Firmware Version : 0x%x\n",aVer->firmwareVersion);
   seq_printf(s,"           ScratchPad : 0x%x\n",aVer->scratchPad);
   seq_printf(s,"        Up Time Count : %u\n",aVer->upTimeCount);

   // seq_printf(s,"             Fd Value : 0x");
   // for (x=0; x < 8; x++) seq_printf(s,"%.02x",aVer->fdValue[8-x]);
   // seq_printf(s,"\n");
   // for (x=0; x < 64; x++)
   // seq_printf(s,"          User Values : 0x%x\n",aVer->userValues[x]);
   // seq_printf(s,"            Device ID : 0x%x\n",aVer->deviceId);

   // Git hash processing to determine if code is 'dirty'
   seq_printf(s,"             Git Hash : ");
   for (x=0; x < 20; x++) {
      if ( aVer->gitHash[19-x] != 0 ) gitDirty = false;
   }
   if ( gitDirty ) {
      seq_printf(s,"dirty (uncommitted code)");
   } else {
      for (x=0; x < 20; x++) seq_printf(s,"%.02x",aVer->gitHash[19-x]);
   }
   seq_printf(s,"\n");

   // Displaying DNA value
   seq_printf(s,"            DNA Value : 0x");
   for (x=0; x < 16; x++) seq_printf(s,"%.02x",aVer->dnaValue[15-x]);
   seq_printf(s,"\n");

   // Build string display
   seq_printf(s,"         Build String : %s\n",aVer->buildString);
}

/**
 * AxiVersion_SetUserReset - Sets the user reset state in the AXI version register.
 * @base: Pointer to the base address of the AXI Version register block.
 * @state: Boolean value indicating the desired state of the user reset; true to set, false to clear.
 *
 * This function sets or clears the user reset bit in the AXI version register based on the
 * provided state. It writes directly to the hardware register to effect this change.
 */
void AxiVersion_SetUserReset(void *base, bool state) {
   struct AxiVersion_Reg *reg = (struct AxiVersion_Reg *)base;
   uint32_t val;

   if (state) {
      val = 0x1;  // Set user reset
   } else {
      val = 0x0;  // Clear user reset
   }

   writel(val, &(reg->userReset));  // Write the value to the userReset register
}
