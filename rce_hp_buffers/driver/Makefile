# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------

NAME := rce_hp
HOME := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
COMP ?= arm-xilinx-linux-gnueabi-
ARCH ?= arm
KDIR ?= /sdf/group/faders/tools/xilinx/rce_linux_kernel/linux-xlnx
KVER := $(shell make -C $(KDIR) -s kernelversion).arm
SRCS := $(wildcard src/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))
GCC  := $(COMP)gcc
TEST := $(shell which $(GCC) 2> /dev/null)

ifndef GITV
	GITT := $(shell cd $(HOME); git describe --tags 2>/dev/null)
	ifeq ($(GITT),)
		GITT := $(shell cd $(HOME); git rev-parse --short HEAD 2>/dev/null)
	endif
	# Final fallback so -DGITV=\"$(GITV)\" always produces a well-formed
	# string literal when building from a non-git source tree (release
	# tarball / exported source). Without this, both git describe and git
	# rev-parse return empty, GITT stays empty, and GITV becomes either
	# "" or "-dirty" — neither is a valid version token.
	ifeq ($(GITT),)
		GITT := unknown
	endif
	GITD := $(shell cd $(HOME); git status --short -uno 2>/dev/null | wc -l)
	GITV := $(if $(filter $(GITD),0),$(GITT),$(GITT)-dirty)
endif

ccflags-y := -I$(HOME)/../../include
ccflags-y += -I$(HOME)/src
ccflags-y += -DDMA_IN_KERNEL=1 -DGITV=\"$(GITV)\"
ccflags-y += -Wformat=0 -Wno-int-to-pointer-cast

$(NAME)-objs := src/dma_buffer.o src/dma_common.o
$(NAME)-objs += src/rce_hp.o src/rce_top.o
obj-m        := $(NAME).o

all:
ifdef TEST
	make ARCH=$(ARCH) CROSS_COMPILE=$(COMP) -C $(KDIR) M=$(HOME) modules
else
	@echo "Skipping compile because $(GCC) not found"
endif

clean:
ifdef TEST
	make -C $(KDIR) M=$(HOME) clean
	rm -f $(OBJS)
endif

