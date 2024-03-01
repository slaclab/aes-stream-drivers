# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description :
# 		Builds the data_gpu kernel driver for aes_stream_drivers package
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------

# Optional path to NVIDIA drivers, if not specified, use empty string.
NVIDIA_DRIVERS ?= ""

# Define the module name.
NAME := datagpu

# Determine the directory of the current Makefile.
HOME := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Kernel version and architecture for building the module.
KVER := $(shell uname -r)
ARCH := $(shell uname -m)

# Cross compilation settings (empty by default).
CROSS_COMPILE :=

# Enable warnings during module post-processing.
KBUILD_MODPOST_WARN := 1

KERNELDIR := /lib/modules/$(KVER)/build

# Source and object files.
SRCS := $(wildcard src/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))

# NVIDIA extra symbols for module linking.
KBUILD_EXTRA_SYMBOLS := $(NVIDIA_DRIVERS)/Module.symvers

# Git versioning: Use tags with optional '-dirty' suffix for uncommitted changes.
ifndef GITV
	GITT := $(shell cd $(HOME); git describe --tags)
	GITD := $(shell cd $(HOME); git status --short -uno | wc -l)
	GITV := $(if $(filter $(GITD),0),$(GITT),$(GITT)-dirty)
endif

# Compiler flags: Include paths and definitions.
ccflags-y += -I$(HOME)/src
ccflags-y += -DDMA_IN_KERNEL=1 -DGITV=\"$(GITV)\"
ccflags-y += -I$(NVIDIA_DRIVERS)/nvidia

# Object files for the module.
$(NAME)-objs := src/dma_buffer.o src/dma_common.o
$(NAME)-objs += src/axi_version.o src/axis_gen2.o src/gpu_async.o src/data_gpu_top.o

# Module target.
obj-m := $(NAME).o

# Default target: Display git version and build the module.
all:
	@echo $(GITV)
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(HOME) modules

# Clean target: Remove built module files and object files.
clean:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(HOME) clean
	rm -f $(OBJS)

