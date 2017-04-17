# ----------------------------------------------------------------------------
# Title      : Data development driver makefile
# ----------------------------------------------------------------------------
# File       : Makefile
# Created    : 2017-03-17
# ----------------------------------------------------------------------------
# Description:
# PGP card driver makefile
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to 
# the license terms in the LICENSE.txt file found in the top-level directory 
# of this distribution and at: 
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
# No part of the aes_stream_drivers package, including this file, may be 
# copied, modified, propagated, or distributed except according to the terms 
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------
 
NAME := datadev
HOME := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
KVER := $(shell uname -r)
SRCS := $(wildcard src/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))

ccflags-y := -I$(HOME)/../../include
ccflags-y += -I$(HOME)/src
ccflags-y += -DDMA_IN_KERNEL=1

$(NAME)-objs := src/dma_buffer.o src/dma_common.o src/fpga_prom.o
$(NAME)-objs += src/axi_version.o src/axis_gen2.o src/data_dev_top.o 
obj-m        := $(NAME).o

all:
	make -C /lib/modules/$(KVER)/build M=$(HOME) modules

clean:
	make -C /lib/modules/$(KVER)/build M=$(HOME) clean
	rm -f $(OBJS)