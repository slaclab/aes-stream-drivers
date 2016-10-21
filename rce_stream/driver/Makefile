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
AXIDMA_HOME := $(shell pwd)
KERNEL := /afs/slac/g/cci/volumes/vol1/xilinx/linux-xlnx

COM_SRC := ../../common/driver
LOC_SRC := src

ccflags-y := -I$(PWD)/../../include
ccflags-y += -I$(PWD)/$(LOC_SRC)
ccflags-y += -I$(PWD)/$(COM_SRC)
ccflags-y += -Wformat=0 -Wno-int-to-pointer-cast

axistream-objs := $(COM_SRC)/dma_buffer.o $(COM_SRC)/dma_common.o 
axistream-objs += $(LOC_SRC)/axis_gen1.o $(LOC_SRC)/axis_top.o

obj-m := axistream.o

all:
	make ARCH=arm CROSS_COMPILE=arm-xilinx-linux-gnueabi- -C $(KERNEL) M=$(AXIDMA_HOME) modules

clean:
	make -C $(KERNEL) M=$(AXIDMA_HOME) clean
	rm -f $(axistream-objs)
