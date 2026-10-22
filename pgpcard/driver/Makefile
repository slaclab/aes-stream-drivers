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

NAME := pgpcard
HOME := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
KVER ?= $(shell uname -r)
ARCH ?= $(shell uname -m)
CROSS_COMPILE :=
KERNELDIR ?= /lib/modules/$(KVER)/build
SRCS := $(wildcard src/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))

# Trust the worktree even when owned by a different user. System/global git
# config is suppressed so a hostile core.fsmonitor / pager / credential helper
# there cannot ride along.
GIT := GIT_CONFIG_SYSTEM=/dev/null GIT_CONFIG_GLOBAL=/dev/null git -c safe.directory='*'

# A packaged source tree under /usr/src carries no git metadata, so the describe
# logic below cannot work there; version.mk supplies GITV instead and
# short-circuits it. Absent in a git worktree, where describe applies as usual.
# $(HOME)-qualified because kbuild re-reads this Makefile from a sub-make whose
# working directory is $(KERNELDIR), where a bare relative path would miss.
-include $(HOME)/version.mk

# Automatically determine the git version
ifndef GITV
    GITT := $(shell cd $(HOME); $(GIT) describe --tags 2>/dev/null)
    ifeq ($(GITT),)
        GITT := $(shell cd $(HOME); $(GIT) rev-parse --short HEAD 2>/dev/null)
    endif
    ifeq ($(GITT),)
        GITT := unknown
    endif
    GITD := $(shell cd $(HOME); $(GIT) status --short -uno 2>/dev/null | wc -l)
    GITV := $(if $(filter $(GITD),0),$(GITT),$(GITT)-dirty)
endif

ccflags-y := -I$(HOME)/../../include
ccflags-y += -I$(HOME)/../include
ccflags-y += -I$(HOME)/src
ccflags-y += -DDMA_IN_KERNEL=1 -DGITV=\"$(GITV)\"
ccflags-y += -Wformat=0 -Wno-int-to-pointer-cast

$(NAME)-objs := src/dma_buffer.o src/dma_common.o src/fpga_prom.o
$(NAME)-objs += src/pgp_common.o src/pgp_gen2.o src/pgp_gen3.o src/pgp_top.o
obj-m        := $(NAME).o

.PHONY: all clean

all:
	@$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(HOME) modules

clean:
	@$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNELDIR) M=$(HOME) clean
	@rm -f $(OBJS)
