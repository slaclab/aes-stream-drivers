/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description: Linux hwmon interface for UltraScale devices implementing the
 *  SysMon IP block. Once registered, it can be viewed with the 'sensors'
 *  command (from lm-sensors), or in sysfs at /sys/class/hwmon/hwmonX
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
#include <linux/version.h>
#include <linux/types.h>
#include <dma_common.h>
#include <axi_hwmon.h>
#include <axi_version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
#include <linux/hwmon.h>
#include <linux/slab.h>

/**
 * Specific hardware types for the FPGA. This is used to choose specific ADC
 * conversion functions.
 */
typedef enum {
   AXI_HWMON_HW_TYPE_ULTRASCALE,
   AXI_HWMON_HW_TYPE_ULTRASCALEPLUS,
} AxiHwmonHwType_t;

/**
 * @brief Private data used by the hwmon driver instance.
 */
struct AxiHwmonDrvPvt {
   AxiHwmonHwType_t hwtype;
   off_t axiVerOffset;
   off_t axiSysmonOffset;
   struct DmaDevice* dev;
   struct device* hwmonDev;
};

static long convSYSMONE1_T(long raw);
static long convSYSMONE4_T(long raw);
static long convSYSMONEx_T(struct AxiHwmonDrvPvt* pvt, long raw);
static long convPS_ADC(struct AxiHwmonDrvPvt* pvt, long raw);

static int AxiHwmon_Read(struct device *dev, enum hwmon_sensor_types type,
                         u32 attr, int channel, long *val);
static int AxiHwmon_ReadString(struct device *dev, enum hwmon_sensor_types type,
                               u32 attr, int channel, const char **str);
static umode_t AxiHwmon_IsVisible(const void *drvdata, enum hwmon_sensor_types type,
                                  u32 attr, int channel);

enum AxiHwmonSensorInstance {
   /**
    * Temperature sensors.
    */
   AXI_HWMON_TEMP = 0,
   AXI_HWMON_TEMP_COUNT, /* Total number of temp sensors */

   /**
    * Voltage sensors.
    */
   AXI_HWMON_VCCINT = 0,
   AXI_HWMON_VCCAUX,
   AXI_HWMON_VCCBRAM,
   AXI_HWMON_IN_COUNT,
};

typedef struct {
   const char* label;
   u32 offset;         /* Offset of the main register (from the IP block base addr) */
   u32 maxOffset;      /* For max level/temp/etc. */
   u32 critOffset;     /* For critical level/temp/etc. */
   u32 highestOffset;  /* For highest recorded value */
   u32 lowestOffset;   /* For lowest recorded value */
   long(*convFunc)(struct AxiHwmonDrvPvt*, long);
} AxiHwmonSensor_t;

#define AXI_HWMON_IN_FLAGS (HWMON_I_INPUT | HWMON_I_LABEL | HWMON_I_HIGHEST | HWMON_I_LOWEST)
#define AXI_HWMON_TEMP_FLAGS (HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT | HWMON_T_LABEL \
                              | HWMON_T_HIGHEST | HWMON_T_LOWEST)

/**
 * Sensor classes.
 */
static const struct hwmon_channel_info* hwmon_ultrascale_channels[] = {
   HWMON_CHANNEL_INFO(temp, AXI_HWMON_TEMP_FLAGS),
   HWMON_CHANNEL_INFO(in, AXI_HWMON_IN_FLAGS),
   NULL,
};

static const struct hwmon_ops hwmon_ultrascale_ops = {
   .is_visible = AxiHwmon_IsVisible,
   .read = AxiHwmon_Read,
   .read_string = AxiHwmon_ReadString,
};

/**
 * Chip info
 */
static const struct hwmon_chip_info hwmon_ultrascale_info = {
   .ops = &hwmon_ultrascale_ops,
   .info = hwmon_ultrascale_channels,
};

/**
 * Number of channels for each sensor class.
 */
static const int hwmon_ultrascale_chan_counts[hwmon_max] = {
   [hwmon_in] = AXI_HWMON_IN_COUNT,
   [hwmon_temp] = AXI_HWMON_TEMP_COUNT,
};

/**
 * Big Ol' array of sensors supported by this device.
 * Nested based on sensor type, because the hwmon API is not amazing...
 */
static const AxiHwmonSensor_t hwmon_ultrascale_sensors[][hwmon_max] = {
   [hwmon_in] = {
      [AXI_HWMON_VCCINT] = {
         .label = "VccInt",
         .offset = 0x404,
         .convFunc = convPS_ADC,
         .highestOffset = 0x484,
         .lowestOffset = 0x494,
      },
      [AXI_HWMON_VCCAUX] = {
         .label = "VccAux",
         .offset = 0x408,
         .convFunc = convPS_ADC,
         .highestOffset = 0x488,
         .lowestOffset = 0x498,
      },
      [AXI_HWMON_VCCBRAM] = {
         .label = "VccBRAM",
         .offset = 0x418,
         .convFunc = convPS_ADC,
         .highestOffset = 0x48C,
         .lowestOffset = 0x49C,
      },
   },
   [hwmon_temp] = {
      [AXI_HWMON_TEMP] = {
         .label = "Temperature",
         .offset = 0x400,
         .maxOffset = 0x55C,
         .critOffset = 0x540,
         .highestOffset = 0x480,
         .lowestOffset = 0x490,
         .convFunc = convSYSMONEx_T,
      },
   }
};

/**
 * Initialize Hwmon sensors for this datadev device.
 * If the device is not supported, this prints a warning and returns. This function returns void
 * because it isn't considered critical in datadev initialization. We'll just eat the error and
 * continue anyways.
 * @param dev The datadev device to init for
 * @param axiVerOffset Offset of the AxiVersion register space
 * @param axiSysMonOffset Offset of the AxiSysMonUltrascale register space
 */
void AxiHwmon_Init(struct DmaDevice* dev, off_t axiVerOffset, off_t axiSysMonOffset) {
   AxiHwmonHwType_t hwtype;

   // Check for support
   switch (readl(dev->base + axiVerOffset + 0x40C)) {
   case 0x0:  // UltraScale/UltraScale+
      break;
   case 0x1:  // 7SERIES (Unsupported)
      return;
   default:
      dev_info(dev->device, "AxiHwmon_Init: No SysMon IP block: hwmon integration will be disabled for this device\n");
      return;
   }

   // Read hardware type and mask off bifurcation index.
   uint32_t htype = readl(dev->base + axiVerOffset + 0x400 + 4 * AxiVersion_Usr_HardwareType_Offset) & 0xFFF;

   // Unfortunately need to case on this. 0x40C does not differentiate between US and US+
   switch (htype) {
   case 0x00000006:  // XilinxAlveoU50
   case 0x00000007:  // XilinxAlveoU200
   case 0x00000008:  // XilinxAlveoU250
   case 0x00000009:  // XilinxAlveoU280
   case 0x0000000C:  // XilinxKcu116
   case 0x0000000E:  // XilinxVcu128
   case 0x0000000F:  // XilinxAlveoU55C
   case 0x00000010:  // XilinxVariumC1100
   case 0x00000013:  // BittWareXupVv8Vu9p
   case 0x00000002:  // BittWareXupVv8Vu13p
      hwtype = AXI_HWMON_HW_TYPE_ULTRASCALEPLUS;
      break;

   case 0x0000000B:  // XilinxKcu105
   case 0x0000000D:  // XilinxKcu1500
   case 0x00000011:  // AbacoPc821Ku085
   case 0x00000012:  // AbacoPc821Ku115
   case 0x00000001:  // AlphaDataKu3
   case 0x00000003:  // SlacPgpCardG3
   case 0x00000004:  // SlacPgpCardG4
      hwtype = AXI_HWMON_HW_TYPE_ULTRASCALE;
      break;

   case 0x00000005:  // XilinxAc701
   case 0x0000000A:  // XilinxKc705
      return;  // Unsupported

   default:
      dev_warn(dev->device, "AxiHwmon_Init: Unknown PCIe hardware type\n");
      return;
   }

   // Allocate the hwmon driver private info
   struct AxiHwmonDrvPvt* pvt = kzalloc(sizeof(struct AxiHwmonDrvPvt), GFP_KERNEL);
   if (!pvt) {
      dev_err(dev->device, "AxiHwmon_Init: Failed to allocate driver private data.\n");
      return;
   }

   pvt->axiSysmonOffset = axiSysMonOffset;
   pvt->axiVerOffset = axiVerOffset;
   pvt->hwtype = hwtype;
   pvt->dev = dev;

   dev->hwmonPrivate = pvt;

   pvt->hwmonDev = hwmon_device_register_with_info(
      dev->device,
      "datadev",
      pvt,
      &hwmon_ultrascale_info,
      NULL);

   // Check for error and clean up
   if (IS_ERR(pvt->hwmonDev)) {
      dev_err(dev->device, "AxiHwmon_Init: Failed to register hwmon device: %ld\n",
              PTR_ERR(pvt->hwmonDev));
      goto error_post_alloc;
   }

   dev_info(dev->device, "AxiHwmon_Init: Registered hwmon device\n");

   return;
error_post_alloc:
   kfree(pvt);
   dev->hwmonPrivate = NULL;
}

/**
 * Removes the hwmon driver instances for this datadevice,
 * freeing any data we allocated along the way.
 */
void AxiHwmon_Remove(struct DmaDevice* dev) {
   struct AxiHwmonDrvPvt* pvt = dev->hwmonPrivate;
   if (!pvt)
      return;

   if (!IS_ERR(pvt->hwmonDev))
      hwmon_device_unregister(pvt->hwmonDev);

   memset(pvt, 0, sizeof(*pvt));
   kfree(pvt);

   dev->hwmonPrivate = NULL;
}

/**
 * @brief SYSMONE1 temperature conversion function. Uses full 16-bit ADC value (not bit shifted).
 * See UG520 equation 2-8.
 * @note Float values have been converted to integers (by multiplication) due to FP being disabled in kernel space.
 * @returns Temperature in mC
 */
static long convSYSMONE1_T(long raw) {
   raw = (raw * 502910) / 65536;
   return raw - 273820;
}

/**
 * @brief SYSMONE4 temperature conversion function. Uses full 16-bit ADC value (not bit shifted).
 * See UG520 equation 2-8
 * @note Float values have been converted to integers (by multiplication) due to FP being disabled in kernel space.
 * @returns Temperature in mC
 */
static long convSYSMONE4_T(long raw) {
   raw = (raw * 507592) / 65536;
   return raw - 279427;
}

/**
 * @brief Uses the correct temperature conversion function based on hardware version
 */
static long convSYSMONEx_T(struct AxiHwmonDrvPvt* pvt, long raw) {
   switch (pvt->hwtype) {
   case AXI_HWMON_HW_TYPE_ULTRASCALE:
      return convSYSMONE1_T(raw);
   case AXI_HWMON_HW_TYPE_ULTRASCALEPLUS:
      return convSYSMONE4_T(raw);
   default:
      return 0;
   }
}

/**
 * @brief 3 volt power supply ADC conversion. Uses full range 16-bit ADC value (not bit shifted).
 * @returns Voltage in mV
 */
static long convPS_ADC(struct AxiHwmonDrvPvt* pvt, long raw) {
   (void) pvt;
   return ((raw * 3 * 1000) / 65536);
}

/**
 * @brief Reads a hwmon channel for the datadev device
 * @param dev Hwmon device structure
 * @param type Sensor type
 * @param attr The attribute (e.g. hwmon_temp_xxx)
 * @param channel The channel index for the sensor type
 * @param val The output value. This will be in mC or mV, depending on the type.
 * @returns 0 on success, non-zero otherwise
 */
static int AxiHwmon_Read(struct device *dev, enum hwmon_sensor_types type,
                         u32 attr, int channel, long *val)
{
   long value;
   uint32_t raw;
   uintptr_t offset;
   int needsValue = 0, needsMax = 0, needsCrit = 0, needsHighest = 0, needsLowest = 0;

   const int typeCount = hwmon_ultrascale_chan_counts[type];
   if (typeCount == 0 || channel >= typeCount || channel < 0) {
      dev_warn(dev, "AxiHwmon_Read: Invalid sensor: chan=%d, type=%d, attr=%d\n", channel, type, attr);
      return -EOPNOTSUPP;
   }

   // Lookup the sensor based on the channel and type
   const AxiHwmonSensor_t* sens = &hwmon_ultrascale_sensors[type][channel];

   struct AxiHwmonDrvPvt* pvt = dev->driver_data;
   if (!pvt) {
      dev_warn(dev, "AxiHwmon_Read: Invalid private data\n");
      return -EINVAL;
   }

   struct DmaDevice* device = pvt->dev;
   if (!device) {
      dev_warn(dev, "AxiHwmon_Read: Invalid DmaDevice\n");
      return -EINVAL;
   }

   // Determine what hwmon wants us to read
   switch (type) {
   case hwmon_temp:
      needsValue = (attr == hwmon_temp_input);
      needsMax = (attr == hwmon_temp_max);
      needsCrit = (attr == hwmon_temp_crit);
      needsHighest = (attr == hwmon_temp_highest);
      needsLowest = (attr == hwmon_temp_lowest);
      break;
   case hwmon_in:  // Voltage, despite the strange name
      needsValue = (attr == hwmon_in_input);
      needsHighest = (attr == hwmon_in_highest);
      needsLowest = (attr == hwmon_in_lowest);
      break;
   default:
      dev_warn(dev, "AxiHwmon_Read: Unknown sensor type\n");
      return -EOPNOTSUPP;
   }

   offset = 0x0;

   // Pick the correct offset
   if (needsValue)
      offset = sens->offset;
   else if (needsMax)
      offset = sens->maxOffset;
   else if (needsCrit)
      offset = sens->critOffset;
   else if (needsLowest)
      offset = sens->lowestOffset;
   else if (needsHighest)
      offset = sens->highestOffset;

   // Reject invalid offsets
   if (offset == 0x0) {
      dev_warn(dev, "AxiHwmon_Read: Invalid offset for sensor type=%d, attr=%d, ch=%d\n",
               type, attr, channel);
      return -EINVAL;
   }

   raw = readl(device->base + pvt->axiSysmonOffset + offset);

   // Do conversions if necessary
   if (sens->convFunc)
      value = sens->convFunc(pvt, raw);
   else
      value = raw;

   *val = value;
   return 0;
}

/**
 * @brief Reads a hwmon string attribute (only label in this case)
 * @param dev The hwmon device to work on
 * @param type Sensor type
 * @param attr The attribute for this sensor type (again, only hwmon_xxx_label here)
 * @param channel Channel index for this senso type
 * @param str Pointer to a string holding the output. Set to NULL for none.
 * @returns 0 on success, non-zero otherwise.
 */
static int AxiHwmon_ReadString(struct device *dev, enum hwmon_sensor_types type,
                               u32 attr, int channel, const char **str)
{
   const char* ptr = NULL;
   const int typeCount = hwmon_ultrascale_chan_counts[type];
   if (typeCount == 0 || channel >= typeCount || channel < 0) {
      dev_warn(dev, "AxiHwmon_Read: Invalid sensor: chan=%d, type=%d, attr=%d\n", channel, type, attr);
      return -EOPNOTSUPP;
   }

   const AxiHwmonSensor_t* sens = &hwmon_ultrascale_sensors[type][channel];
   switch (type) {
   case hwmon_temp:
      if (attr == hwmon_temp_label)
         ptr = sens->label;
      break;
   case hwmon_in:
      if (attr == hwmon_in_label)
         ptr = sens->label;
      break;
   default:
      break;
   }

   if (ptr)
      *str = ptr;
   else
      return -EOPNOTSUPP;
   return 0;
}

/**
 * @brief Simply returns the access attributes for the hwmon instance.
 * Always 0444 in our case because they're read-only.
 */
static umode_t AxiHwmon_IsVisible(const void *drvdata, enum hwmon_sensor_types type,
                                  u32 attr, int channel)
{
   return 0444;
}
#else

/**
 * Stubs for older kernels.
 */

void AxiHwmon_Init(struct DmaDevice* dev, off_t axiVerOffset, off_t axiSysMonOffset) {}
void AxiHwmon_Remove(struct DmaDevice* dev) {}

#endif  // LINUX_VERSION_CODE >= KERNEL_VERSION(5,1,0)
