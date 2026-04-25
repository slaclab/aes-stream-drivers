# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description:
#     Cross-validation harness for emu_prbs_flfsr vs PrbsData::flfsr.
#     Builds a userspace binary that compiles emulator/driver/src/prbs.c
#     against the shim headers under kernel_shim/ and links it to
#     common/app_lib/PrbsData.cpp, then runs a 1M-iteration LFSR
#     equivalence loop plus a byte-for-byte frame-layout comparison.
#
#     `make test` exits 0 iff the kernel PRBS and the userspace PRBS
#     agree. See emulator/driver/tests/prbs_cross_validate_cxx.cpp for
#     the real main (Task 01-04-02) and prbs_cross_validate.c for the
#     userspace-compile shim that pulls prbs.c in unchanged.
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------

REPO_ROOT := $(abspath ../../..)
APP_LIB   := $(REPO_ROOT)/common/app_lib
SHIM_DIR  := $(abspath kernel_shim)

CC        ?= gcc
CXX       ?= g++
CFLAGS    ?= -Wall -Wextra -O2 -std=gnu11
CXXFLAGS  ?= -Wall -Wextra -O2 -std=gnu++17
LDFLAGS   ?=

# Shim path MUST come first so <linux/types.h> etc. resolve to the
# stubs under kernel_shim/linux/ instead of the real kernel headers.
CFLAGS_SHIM := -I$(SHIM_DIR)
CXX_INCLUDES := -I$(APP_LIB)

BIN := prbs_cross_validate

OBJS := prbs_cross_validate.o prbs_cross_validate_cxx.o PrbsData.o

# Userspace miscdevice smoke harness. Links pure glibc; no
# kernel_shim, no PrbsData.cpp. Includes emu_gpu_addr_table.h as a
# userspace TU via the stub's src/ directory.
STUB_BIN := stub_mmap_test
STUB_OBJS := stub_mmap_test.o
STUB_INCLUDES := -I$(REPO_ROOT)/emulator/gpu_stub/src

.PHONY: all test clean

all: $(BIN) $(STUB_BIN)

prbs_cross_validate.o: prbs_cross_validate.c
	$(CC) $(CFLAGS) $(CFLAGS_SHIM) -c $< -o $@

prbs_cross_validate_cxx.o: prbs_cross_validate_cxx.cpp
	$(CXX) $(CXXFLAGS) $(CXX_INCLUDES) -c $< -o $@

PrbsData.o: $(APP_LIB)/PrbsData.cpp
	$(CXX) $(CXXFLAGS) $(CXX_INCLUDES) -c $< -o $@

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)

# Userspace C compile, no shim.
stub_mmap_test.o: stub_mmap_test.c
	$(CC) $(CFLAGS) $(STUB_INCLUDES) -c $< -o $@

$(STUB_BIN): $(STUB_OBJS)
	$(CC) $(CFLAGS) $(STUB_OBJS) -o $@ $(LDFLAGS)

test: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) $(OBJS) $(STUB_BIN) $(STUB_OBJS)
