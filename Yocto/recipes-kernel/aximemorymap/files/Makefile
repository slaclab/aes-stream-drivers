# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description :
# 		Builds the axi_memory_map kernel driver for petalinux package
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------

# Name of the module
NAME := axi_memory_map

# Determine the directory of the current Makefile
HOME := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Versioning information from Git for embedding in the driver
GITV := petalinux

# Source and object files
SRCS := $(wildcard $(HOME)/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))

# Compiler flags
ccflags-y := -I$(HOME)
ccflags-y += -DDMA_IN_KERNEL=1 -DGITV=\"$(GITV)\"
ccflags-y += -Wformat=0 -Wno-int-to-pointer-cast
ccflags-y += -g -DDEBUG

# Object files for the module
$(NAME)-objs := aximemorymap.o
obj-m := $(NAME).o

# Default target to compile the kernel module
all:
	@$(MAKE) -C $(KERNEL_SRC) M=$(HOME)

# Target to install the module
modules_install:
	@$(MAKE) -C $(KERNEL_SRC) M=$(HOME) modules_install

# Clean the build directory
clean:
	@rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	@rm -f Module.markers Module.symvers modules.order
	@rm -rf .tmp_versions Modules.symvers
