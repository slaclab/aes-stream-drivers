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

# Determine if the NVIDIA drivers
ifneq ($(NVIDIA_DRIVERS),)
$(info NVIDIA drivers are in-use for this build. NVIDIA_DRIVERS=$(NVIDIA_DRIVERS))
endif

# Determine the current directory of the Makefile
HOME := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Kernel version and architecture for the build
KVER := $(shell uname -r)
ARCH := $(shell uname -m)

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
    GITT := $(shell cd $(HOME); git describe --tags)
    GITD := $(shell cd $(HOME); git status --short -uno | wc -l)
    GITV := $(if $(filter $(GITD),0),$(GITT),$(GITT)-dirty)
endif

# Compiler flags, including path to headers and version definitions
ccflags-y += -I$(HOME)/src
ccflags-y += -DDMA_IN_KERNEL=1 -DGITV=\"$(GITV)\"

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


# Clean target: remove built module and object files
clean:
	@$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(HOME) clean
	@rm -f $(OBJS)
