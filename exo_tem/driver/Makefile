# ----------------------------------------------------------------------------
# Title      : TEM driver makefile
# ----------------------------------------------------------------------------
# File       : Makefile
# Author     : Ryan Herbst, rherbst@slac.stanford.edu
# Created    : 2016-08-08
# Last update: 2016-08-08
# ----------------------------------------------------------------------------
# Description:
# TEM card driver makefile
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
TEMCARD_HOME := $(shell pwd)
TEMCARD_KVER := $(shell uname -r)

COM_SRC := ../../common/driver
LOC_SRC := src

ccflags-y := -I$(PWD)/../../include
ccflags-y += -I$(PWD)/$(LOC_SRC)
ccflags-y += -I$(PWD)/$(COM_SRC)
ccflags-y += -DDMA_IN_KERNEL=1

temcard-objs := $(COM_SRC)/dma_buffer.o $(COM_SRC)/dma_common.o 
temcard-objs += $(LOC_SRC)/tem_gen3.o $(LOC_SRC)/tem_top.o

obj-m := temcard.o

all:
	make -C /lib/modules/$(TEMCARD_KVER)/build M=$(TEMCARD_HOME) modules

clean:
	make -C /lib/modules/$(TEMCARD_KVER)/build M=$(TEMCARD_HOME) clean
	rm -f $(temcard-objs)
