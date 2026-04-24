# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description :
# 		Builds the data_dev kernel driver for aes_stream_drivers package
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------

# Name of the module to be built
NAME := datadev

# DKMS PRE_BUILD (build-nvidia.sh) writes NVIDIA_DRIVERS=<src_tree>/<nvidia-ver>
# into Makefile.local when a matching NVIDIA DKMS tree is installed.  `-include`
# (hyphen prefix) silently skips the file when absent — which is the expected
# case on CI/emulator hosts where build-nvidia.sh truncates Makefile.local to
# zero bytes and the GPU path is built without NVIDIA symbols.
-include Makefile.local

# Determine if the NVIDIA drivers
ifneq ($(NVIDIA_DRIVERS),)
$(info NVIDIA drivers are in-use for this build. NVIDIA_DRIVERS=$(NVIDIA_DRIVERS))
endif

# Determine the current directory of the Makefile
HOME := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Kernel version and architecture for the build
KVER ?= $(shell uname -r)
ARCH ?= $(shell uname -m)

# Cross-compile prefix, if any
CROSS_COMPILE :=

# Enable warnings during module post-processing.
KBUILD_MODPOST_WARN := 1

# Directory containing the kernel build system
KERNELDIR := /lib/modules/$(KVER)/build

# Source and object files
SRCS := $(wildcard src/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))

ifneq ($(NVIDIA_DRIVERS),)
# NVIDIA extra symbols for module linking.
KBUILD_EXTRA_SYMBOLS := $(NVIDIA_DRIVERS)/Module.symvers
endif

# Automatically determine the git version
ifndef GITV
    GITT := $(shell cd $(HOME); git describe --tags 2>/dev/null)
    ifeq ($(GITT),)
        GITT := $(shell cd $(HOME); git rev-parse --short HEAD 2>/dev/null)
    endif
    ifeq ($(GITT),)
        GITT := emulator
    endif
    GITD := $(shell cd $(HOME); git status --short -uno | wc -l)
    GITV := $(if $(filter $(GITD),0),$(GITT),$(GITT)-dirty)
endif

# Compiler flags, including path to headers and version definitions
ccflags-y += -I$(HOME)/src
ccflags-y += -DDMA_IN_KERNEL=1 -DPCIE_DMA=1 -DGITV=\"$(GITV)\"

ifneq ($(NVIDIA_DRIVERS),)
ccflags-y += -I$(NVIDIA_DRIVERS)/nvidia -I$(NVIDIA_DRIVERS)/kernel-open/nvidia -DDATA_GPU=1
endif

$(info $(ccflags-y))

# Object files that make up the module
$(NAME)-objs := src/dma_buffer.o src/dma_common.o
$(NAME)-objs += src/axi_version.o src/axis_gen2.o src/data_dev_top.o

ifneq ($(NVIDIA_DRIVERS),)
$(NAME)-objs += src/gpu_async.o
endif

# Target module to be built
obj-m := $(NAME).o

# Default target: build the kernel module
all: rc.local
	@echo "Building with version: $(GITV)"
	@$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(HOME) modules

# construct a rc.local file for the location of this driver
rc.local:
	sed 's|DRIVER_LOCATION|$(HOME)|' rc.local.in > rc.local
	chmod +x rc.local


# Clean target: remove built module and object files
clean:
	@$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(HOME) clean
	@rm -f $(OBJS)

define dkms_configure
	rm -rf $(1)
	mkdir -p $(1)/dkms_main_tree
	mkdir -p $(1)/dkms_source_tree
	echo "2.0.0" > $(1)/dkms_main_tree/dkms_dbversion
	cp -r .dkms-common/. $(1)/dkms_source_tree/
	cp -vL $(3) $(1)/dkms_source_tree/dkms.conf
	$(if $(4),cp -vL $(4) $(1)/dkms_source_tree/)
	cp -vL rc.local.in $(1)/dkms_source_tree/
	echo "PACKAGE_VERSION=$(GITV)"	 					>> $(1)/dkms_source_tree/dkms.conf
	echo "$(NAME)" 										>> $(1)/dkms_source_tree/$(2)_load.conf
	echo "#!/bin/bash" 									 > $(1)/dkms_source_tree/install.sh
	echo "dkms add -m $(2) -v $(GITV)" 					>> $(1)/dkms_source_tree/install.sh
	echo "dkms build -m $(2) -v $(GITV) -a $(ARCH)"	 	>> $(1)/dkms_source_tree/install.sh
	echo "dkms install -m $(2) -v $(GITV) -a $(ARCH)" 	>> $(1)/dkms_source_tree/install.sh
	echo "cp $(2)_load.conf /etc/modules-load.d" 		>> $(1)/dkms_source_tree/install.sh
	echo "modprobe $(NAME)" 							>> $(1)/dkms_source_tree/install.sh
	chmod a+rx $(1)/dkms_source_tree/install.sh
	tar -czf $@ --exclude=*.o -C $(1) dkms_main_tree dkms_source_tree
endef

# Create a common dkms staging directory.  Force a fresh rebuild on every
# `make dkms` invocation so a stale/corrupt .dkms-common (e.g. leftover NFS
# silly-renames from an interrupted container) cannot poison the tarball
# contents.  The directory is a PHONY target, so make always runs the recipe.
.PHONY: .dkms-common
.dkms-common:
	rm -rf .dkms-common
	mkdir -p .dkms-common/src
	cp -rLv src/* .dkms-common/src/
	cp -L Makefile .dkms-common/
	cp -L datadev.conf .dkms-common/

# Datadev CPU dkms
# DKMS 2.8 (ubuntu:22.04) requires the native mktarball layout at the
# archive root: dkms_main_tree/dkms_dbversion + dkms_source_tree/dkms.conf.
# DKMS 3.x is lenient (falls back to `find -name dkms.conf`), which is why
# the earlier <PACKAGE_NAME>-<VERSION>/ layout worked everywhere except
# ubuntu:22.04.  This layout is accepted by both DKMS 2.x and 3.x.
datadev-dkms-$(GITV).tar.gz: .dkms-common
	$(call dkms_configure,.dkms-datadev,datadev,dkms.conf)

# Datadev GPU dkms -- matches PACKAGE_NAME=datadev-gpu-dkms in dkms-gpu.conf.
datadev-gpu-dkms-$(GITV).tar.gz: .dkms-common
	$(call dkms_configure,.dkms-datadev-gpu,datadev-gpu,dkms-gpu.conf,build-nvidia.sh)

dkms: datadev-dkms-$(GITV).tar.gz datadev-gpu-dkms-$(GITV).tar.gz

.PHONY: dkms