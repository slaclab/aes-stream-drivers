# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description :
# 		Builds the axi_stream_dma kernel driver for Yocto package
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------

# Kernel module name
NAME := axi_stream_dma

# Directory containing the Makefile
HOME := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Trust the worktree even when owned by a different user — required when
# /etc/rc.local rebuilds drivers as root from a tree cloned by an
# unprivileged user. System/global git config is suppressed so a hostile
# core.fsmonitor / pager / credential helper there cannot ride along.
GIT := GIT_CONFIG_SYSTEM=/dev/null GIT_CONFIG_GLOBAL=/dev/null git -c safe.directory='*'

# Automatically determine the git version (override via GITV=... from caller, e.g. recipe)
ifndef GITV
    GITT := $(shell cd $(HOME); $(GIT) describe --tags 2>/dev/null)
    ifeq ($(GITT),)
        GITT := $(shell cd $(HOME); $(GIT) rev-parse --short HEAD 2>/dev/null)
    endif
    ifeq ($(GITT),)
        GITT := emulator
    endif
    GITD := $(shell cd $(HOME); $(GIT) status --short -uno 2>/dev/null | wc -l)
    GITV := $(if $(filter $(GITD),0),$(GITT),$(GITT)-dirty)
endif

# Source files discovery within the same directory as the Makefile
SRCS := $(wildcard $(HOME)/*.c)

# Object files corresponding to the source files
OBJS := $(patsubst %.c,%.o,$(SRCS))

# Compiler flags
# - Include the directory of the Makefile for header file searches
# - Define macros relevant to the DMA and debug options
# - Suppress specific warnings and enable debugging symbols
ccflags-y := -I$(HOME)
ccflags-y += -DDMA_IN_KERNEL=1 -DGITV=\"$(GITV)\"
ccflags-y += -Wformat=0 -Wno-int-to-pointer-cast
ccflags-y += -g -DDEBUG
ccflags-y += "-DDMA_TX_BUFF_COUNT=$(DMA_TX_BUFF_COUNT)" "-DDMA_RX_BUFF_COUNT=$(DMA_RX_BUFF_COUNT)" "-DDMA_BUFF_SIZE=$(DMA_BUFF_SIZE)"

# Object files needed for the kernel module
$(NAME)-objs := dma_buffer.o dma_common.o axis_gen1.o axis_gen2.o axistreamdma.o

# Kernel module object
obj-m := $(NAME).o

# Default target: build the kernel module
all:
	@echo "Building $(NAME) kernel module..."
	$(MAKE) -C $(KERNEL_SRC) M=$(HOME)

# Install the built kernel module into the system
modules_install:
	@echo "Installing $(NAME) module..."
	$(MAKE) -C $(KERNEL_SRC) M=$(HOME) modules_install

# Clean the build directory
clean:
	@echo "Cleaning up build artifacts..."
	rm -f $(HOME)/*.o $(HOME)/*~ $(HOME)/core $(HOME)/.depend $(HOME)/.*.cmd $(HOME)/*.ko $(HOME)/*.mod.c
	rm -f $(HOME)/Module.markers $(HOME)/Module.symvers $(HOME)/modules.order
	rm -rf $(HOME)/.tmp_versions $(HOME)/Modules.symvers
