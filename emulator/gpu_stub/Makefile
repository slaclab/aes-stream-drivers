# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description:
#     Builds the nvidia_p2p_stub kernel module for CI testing.
#     Exports fake nvidia_p2p_* symbols so the datadev driver can be
#     compiled with DATA_GPU and loaded against the emulator without a
#     real NVIDIA GPU or NVIDIA kernel driver.
# ----------------------------------------------------------------------------

# Name of the module
NAME := nvidia_p2p_stub

# Determine the current directory
HOME := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Kernel version and architecture
KVER ?= $(shell uname -r)
ARCH ?= $(shell uname -m)

# Directory containing the kernel build system
KERNELDIR := /lib/modules/$(KVER)/build

# Compiler flags — include path for nv-p2p.h (local canonical copy)
ccflags-y += -I$(HOME)/include
# BUILD_VER: epoch timestamp stamped into the .ko and written to the sideband
# .build_version file for the Pitfall-6 staleness gate. Uses ?= so the RHS is
# a deferred-expansion recursive variable — the $(shell date +%s) only fires
# when $(BUILD_VER) is actually referenced (i.e., during a modules build),
# not on `make clean`, `make -n`, or tab-completion. The `all:` recipe
# resolves BUILD_VER once and propagates it to kbuild's sub-make so the
# sideband file and the compiled .ko always carry identical timestamps.
BUILD_VER ?= $(shell date +%s)
ccflags-y += -DEMU_BUILD_VERSION=$(BUILD_VER)

# Object files that make up the module
$(NAME)-objs := src/nvidia_p2p_stub.o src/emu_gpu_addr_table.o

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
	 $(MAKE) BUILD_VER=$$BUILD_VER ARCH=$(ARCH) -C $(KERNELDIR) M=$(HOME) modules

# Clean target
clean:
	@$(MAKE) ARCH=$(ARCH) -C $(KERNELDIR) M=$(HOME) clean

.PHONY: all clean
