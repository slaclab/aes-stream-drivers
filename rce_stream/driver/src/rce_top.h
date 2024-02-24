/**
 *-----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 *-----------------------------------------------------------------------------
 * Description:
 *    This header file provides essential definitions, configurations, and
 *    interface declarations for the development of kernel-level drivers
 *    and software components within the RCE (Reconfigurable Computing
 *    Environment) project. It ensures consistency and standardization
 *    across various modules and components.
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

#ifndef __RCE_TOP_H__
#define __RCE_TOP_H__

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
 *
 * Return: 0 on success, error code on failure.
 */
int Rce_Remove(struct platform_device *pdev);

#endif // __RCE_TOP_H__
