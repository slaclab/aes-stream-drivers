# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description:
#     Makefile for building drivers, applications, and RCE modules.
#     This Makefile provides targets for compiling applications (app),
#     drivers, and RCE modules for specific architectures and kernel versions.
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------

# Set the path to the Makefile's directory
MAKE_HOME := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Default architecture
ARCH ?= arm

# Discover the local kernel versions
LOC_VERS := $(wildcard /lib/modules/*/build)
LOC_VERS := $(patsubst %/,%,$(dir $(LOC_VERS)))
LOC_VERS := $(notdir $(LOC_VERS))

# Define RCE module directories
RCE_DIRS := /sdf/group/faders/tools/xilinx/rce_linux_kernel/linux-xlnx-v2016.4
RCE_DIRS += /sdf/group/faders/tools/xilinx/rce_linux_kernel/backup/linux-xlnx-v2016.1.01

# Default target: Display the available build options
all:
	@echo "Available targets: app driver rce"

# Build applications
app:
	@echo "Building applications..."
	@make -C $(MAKE_HOME)/rce_stream/app clean
	@make -C $(MAKE_HOME)/rce_stream/app
	@make -C $(MAKE_HOME)/data_dev/app clean
	@make -C $(MAKE_HOME)/data_dev/app

# Build drivers for each discovered kernel version
driver:
	@echo "Building drivers for all kernel versions..."
	@mkdir -p $(MAKE_HOME)/install
	@$(foreach ver,$(LOC_VERS), \
		mkdir -p $(MAKE_HOME)/install/$(ver); \
		make -C $(MAKE_HOME)/data_dev/driver KVER=$(ver) clean; \
		make -C $(MAKE_HOME)/data_dev/driver KVER=$(ver); \
		scp $(MAKE_HOME)/data_dev/driver/*.ko $(MAKE_HOME)/install/$(ver); \
	)

# Build RCE modules for specified directories
rce:
	@echo "Building RCE modules..."
	@mkdir -p $(MAKE_HOME)/install
	@$(foreach d,$(RCE_DIRS), \
		mkdir -p $(MAKE_HOME)/install/$(shell make -C $(d) -s kernelversion).$(ARCH); \
		make -C $(MAKE_HOME)/rce_stream/driver KDIR=$(d) clean; \
		make -C $(MAKE_HOME)/rce_stream/driver KDIR=$(d); \
		scp $(MAKE_HOME)/rce_stream/driver/*.ko $(MAKE_HOME)/install/$(shell make -C $(d) -s kernelversion).$(ARCH)/; \
		make -C $(MAKE_HOME)/rce_memmap/driver KDIR=$(d) clean; \
		make -C $(MAKE_HOME)/rce_memmap/driver KDIR=$(d); \
		scp $(MAKE_HOME)/rce_memmap/driver/*.ko $(MAKE_HOME)/install/$(shell make -C $(d) -s kernelversion).$(ARCH)/; \
	)
