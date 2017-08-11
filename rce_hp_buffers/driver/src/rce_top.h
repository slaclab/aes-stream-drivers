/**
 *-----------------------------------------------------------------------------
 * Title      : Top level module
 * ----------------------------------------------------------------------------
 * File       : rce_top.h
 * Author     : Ryan Herbst, rherbst@slac.stanford.edu
 * Created    : 2017-08-11
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
#ifndef __RCE_TOP_H__
#define __RCE_TOP_H__

#include <linux/types.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#define MAX_DMA_DEVICES 1

// Create and init device
int Rce_Probe(struct platform_device *pdev);

// Cleanup device
int Rce_Remove(struct platform_device *pdev);

#endif

