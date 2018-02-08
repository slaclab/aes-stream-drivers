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

NAME := temcard
HOME := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
KVER := $(shell uname -r)
SRCS := $(wildcard src/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))
GITT := $(shell cd $(HOME); git describe --tags)
GITD := $(shell cd $(HOME); git status --short -uno | wc -l)
GITV := $(if $(filter $(GITD),0),$(GITT),$(GITT)-dirty)

ccflags-y := -I$(HOME)/../../include
ccflags-y += -I$(HOME)/src
ccflags-y += -DDMA_IN_KERNEL=1 -DGITV=\"$(GITV)\"

$(NAME)-objs := src/dma_buffer.o src/dma_common.o src/fpga_prom.o
$(NAME)-objs += src/tem_gen3.o src/tem_top.o
obj-m        := $(NAME).o

all:
	make -C /lib/modules/$(KVER)/build M=$(HOME) modules

clean:
	make -C /lib/modules/$(KVER)/build M=$(HOME) clean
	rm -f $(OBJS)

