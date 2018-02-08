# ----------------------------------------------------------------------------
# Title      : RCE stream driver makefile
# ----------------------------------------------------------------------------
# File       : Makefile
# Author     : Ryan Herbst, rherbst@slac.stanford.edu
# Created    : 2016-08-08
# Last update: 2016-08-08
# ----------------------------------------------------------------------------
# Description:
# RCE stream driver makefile
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to 
# the license terms in the LICENSE.txt file found in the top-level directory 
# of this distribution and at: 
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
# No part of the aes_stream_drivers package, including this file, may be 
# copied, modified, propagated, or distributed except according to the terms 
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------
# 

NAME := rcestream
HOME := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
GITV := $(shell cd $(HOME); git describe --tags)
COMP := arm-xilinx-linux-gnueabi-
ARCH := arm
KDIR := /afs/slac/g/cci/volumes/vol1/xilinx/linux-xlnx
KVER := $(shell make -C $(KDIR) -s kernelversion).arm
SRCS := $(wildcard src/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))
GCC  := $(COMP)gcc
TEST := $(shell which $(GCC) 2> /dev/null)

ccflags-y := -I$(HOME)/../../include
ccflags-y += -I$(HOME)/src
ccflags-y += -DDMA_IN_KERNEL=1 -DGITV=\"$(GITV)\"
ccflags-y += -Wformat=0 -Wno-int-to-pointer-cast

$(NAME)-objs := src/dma_buffer.o src/dma_common.o 
$(NAME)-objs += src/axis_gen1.o src/axis_gen2.o src/rce_top.o
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

