# ----------------------------------------------------------------------------
# Title      : PGP driver makefile
# ----------------------------------------------------------------------------
# File       : Makefile
# Author     : Ryan Herbst, rherbst@slac.stanford.edu
# Created    : 2016-08-08
# Last update: 2016-08-08
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
# 
PGPCARD_HOME := $(shell pwd)
PGPCARD_KVER := $(shell uname -r)

COM_SRC := ../../common/driver
LOC_SRC := src

ccflags-y := -I$(PWD)/../../include
ccflags-y += -I$(PWD)/$(LOC_SRC)
ccflags-y += -I$(PWD)/$(COM_SRC)
ccflags-y += -DDMA_IN_KERNEL=1

pgpcard-objs := $(COM_SRC)/dma_buffer.o $(COM_SRC)/dma_common.o 
pgpcard-objs += $(LOC_SRC)/pgp_common.o $(LOC_SRC)/pgp_gen2.o $(LOC_SRC)/pgp_gen3.o
pgpcard-objs += $(LOC_SRC)/pgp_top.o

obj-m := pgpcard.o

all:
	make -C /lib/modules/$(PGPCARD_KVER)/build M=$(PGPCARD_HOME) modules

clean:
	make -C /lib/modules/$(PGPCARD_KVER)/build M=$(PGPCARD_HOME) clean
	rm -f $(pgpcard-objs)
