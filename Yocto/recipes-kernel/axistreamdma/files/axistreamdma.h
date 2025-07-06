/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    This file implements a Linux kernel driver for managing DMA (Direct Memory Access)
 *    operations via AXI (Advanced eXtensible Interface) Stream interfaces. It supports
 *    the initialization, configuration, and runtime management of DMA transfers between
 *    AXI Stream peripherals and system memory, optimizing data handling for high-speed
 *    data acquisition and processing systems.
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

#ifndef __AXI_STREAM_DMA_H__
#define __AXI_STREAM_DMA_H__

#include <linux/types.h>
#include <linux/device.h>
#include <linux/platform_device.h>

/**
 * MAX_DMA_DEVICES - Maximum number of DMA devices supported.
 */
#define MAX_DMA_DEVICES 4

/**
 * Rce_Probe - Probe function for RCE platform devices.
 * @pdev: Platform device structure.
 *
 * This function initializes the device driver for a given platform device.
 * It is called by the Linux kernel when the device is detected.
 *
 * Return: 0 on success, error code on failure.
 */
int Rce_Probe(struct platform_device *pdev);

/**
 * Rce_Remove - Remove function for RCE platform devices.
 * @pdev: Platform device structure.
 *
 * This function performs the cleanup and removal of the device driver
 * associated with a given platform device. It is called by the Linux
 * kernel during the device removal process.
 */
void Rce_Remove(struct platform_device *pdev);

#endif  // __AXI_STREAM_DMA_H__
