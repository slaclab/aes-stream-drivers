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
ARCH := $(shell uname -m)
CROSS_COMPILE :=
KERNELDIR := /lib/modules/$(KVER)/build
SRCS := $(wildcard src/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))

ifndef GITV
	GITT := $(shell cd $(HOME); git describe --tags)
	GITD := $(shell cd $(HOME); git status --short -uno | wc -l)
	GITV := $(if $(filter $(GITD),0),$(GITT),$(GITT)-dirty)
endif

ccflags-y += -I$(HOME)/src
ccflags-y += -DDMA_IN_KERNEL=1 -DGITV=\"$(GITV)\"

$(NAME)-objs := src/dma_buffer.o src/dma_common.o
$(NAME)-objs += src/axi_version.o src/axis_gen2.o src/data_dev_top.o
obj-m        := $(NAME).o

all:
	@echo $(GITV)
	make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(HOME) modules

clean:
	make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(HOME) clean
	rm -f $(OBJS)
