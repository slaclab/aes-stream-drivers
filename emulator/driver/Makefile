# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description:
#     Builds the datadev_emulator kernel module for CI testing.
#     This module creates a virtual PCI device that the real datadev
#     driver can probe without physical FPGA hardware.
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
NAME := datadev_emulator

# Determine the current directory
HOME := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Kernel version and architecture
KVER ?= $(shell uname -r)
ARCH ?= $(shell uname -m)

# Directory containing the kernel build system
KERNELDIR := /lib/modules/$(KVER)/build

# Compiler flags
ccflags-y += -I$(HOME)/src
# BUILD_VER: epoch timestamp stamped into the .ko and written to the sideband
# .build_version file for the Pitfall-6 staleness gate. Uses ?= so the RHS is
# a deferred-expansion recursive variable — the $(shell date +%s) only fires
# when $(BUILD_VER) is actually referenced (i.e., during a modules build),
# not on `make clean`, `make -n`, or tab-completion. The `all:` recipe
# resolves BUILD_VER once and propagates it to kbuild's sub-make so the
# sideband file and the compiled .ko always carry identical timestamps.
BUILD_VER ?= $(shell date +%s)
ccflags-y += -DEMU_BUILD_VERSION=$(BUILD_VER)

# Pick up cross-module symbols from nvidia_p2p_stub.
# When the stub has been built before us (normal CI/developer flow), use its
# Module.symvers so modpost can resolve emu_gpu_register_drain_cb and friends.
# Falls back cleanly if the file is absent (standalone build without stub).
STUB_SYMVERS := $(HOME)/../gpu_stub/Module.symvers
ifneq ($(wildcard $(STUB_SYMVERS)),)
  KBUILD_EXTRA_SYMBOLS_ARG := KBUILD_EXTRA_SYMBOLS=$(STUB_SYMVERS)
else
  KBUILD_EXTRA_SYMBOLS_ARG :=
endif

# Object files that make up the module
$(NAME)-objs := src/bar0_regs.o src/virt_irq.o src/virt_pci_host.o src/dma_engine.o src/prbs.o src/gpu_engine.o src/emu_main.o

# Target module
obj-m := $(NAME).o

# Default target: build the kernel module.
# Resolve BUILD_VER once in the recipe shell, write it to the sideband file,
# and override it into kbuild's sub-make so the .ko's EMU_BUILD_VERSION macro
# matches the value in $(HOME)/.build_version. This keeps the Pitfall-6
# staleness gate honest (the wrapper compares modinfo output to this file),
# and keeps the file write off the Make expansion phase so `make clean`,
# `make -n`, and tab-completion don't desynchronize the stamp from the .ko.
all:
	@echo "Building $(NAME) with kernel $(KVER)"
	@BUILD_VER=$$(date +%s); \
	 printf '%s\n' "$$BUILD_VER" > $(HOME)/.build_version; \
	 $(MAKE) BUILD_VER=$$BUILD_VER ARCH=$(ARCH) -C $(KERNELDIR) M=$(HOME) \
	   $(KBUILD_EXTRA_SYMBOLS_ARG) modules

# Clean target.
# Also removes the sideband .build_version stamp written by the `all` recipe
# so `make clean` leaves a truly-clean tree; otherwise the Pitfall-6
# staleness gate (modinfo vs .build_version) would report a stale match
# against whatever epoch last built here.
clean:
	@$(MAKE) ARCH=$(ARCH) -C $(KERNELDIR) M=$(HOME) clean
	@rm -f $(HOME)/.build_version

.PHONY: all clean
