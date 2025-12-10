# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description :
#     Makefile for building application.
#     This Makefile demonstrates how to compile a project that consists of
#     a library and an application using the library.
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------

# Variables
HOME     := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
CC       := $(CROSS_COMPILE)g++
DEF      :=
BIN      := $(HOME)/bin
OBJ      := $(HOME)/.obj
CFLAGS   := -O2 -Wall -I$(HOME)/../../include -I$(HOME)/../../common/app_lib -fpic --std=c++11 -DDEFAULT_AXI_DEVICE=\"/dev/datadev_0\"
LFLAGS   := -lpthread
NVCC     ?= /usr/local/cuda/bin/nvcc
NVCCFLAGS := -O2 -I$(HOME)/../../include -I$(HOME)/../../common/app_lib --gpu-architecture=compute_100 -lcuda

# Source and Object Directories
LIB_DIR  := $(HOME)/../../common/app_lib
LIB_SRC  := $(wildcard $(LIB_DIR)/*.cpp)
LIB_HDR  := $(wildcard $(LIB_DIR)/*.h)
LIB_OBJ  := $(patsubst $(LIB_DIR)/%.cpp,$(OBJ)/%.o,$(LIB_SRC))

APP_DIR  := $(HOME)/src
APP_SRC  := $(wildcard $(APP_DIR)/*.cpp)
APP_BIN  := $(addprefix $(BIN)/,dmaRate dmaWrite dmaSetDebug test)

CUDA_BIN := $(addprefix $(BIN)/,rdmaTest)

# Main Targets
all: $(LIB_OBJ) $(APP_BIN)
	@echo "Build complete."

# CUDA targets
cuda: $(LIB_OBJ) $(CUDA_BIN)

# Phony Targets
.PHONY: all clean nothing_done

# Clean Build Artifacts
clean:
	@echo "Cleaning build artifacts."
	@rm -f $(BIN)/*
	@rm -f $(OBJ)/*

# Compile Library Objects
$(OBJ)/%.o: $(LIB_DIR)/%.cpp $(LIB_DIR)/%.h
	@test -d $(OBJ) || mkdir -p $(OBJ)
	@echo "Compiling library object $@"
	@$(CC) -c $(CFLAGS) $(DEF) -o $@ $<

# Compile Application Binaries
$(BIN)/%: $(APP_DIR)/%.cpp $(LIB_OBJ)
	@test -d $(BIN) || mkdir -p $(BIN)
	@echo "Compiling application binary $@"
	@$(CC) $(CFLAGS) $(DEF) $(LIB_OBJ) -o $@ $< $(LFLAGS)

# Compile Application Binaries
$(BIN)/rdmaTest: $(APP_DIR)/rdmaTest.cu $(LIB_OBJ)
	@test -d $(BIN) || mkdir -p $(BIN)
	@echo "Compiling application binary $@"
	@$(NVCC) $(NVCCFLAGS) $(DEF) $(LIB_OBJ) -o $@ $< $(LFLAGS)

# Check if anything was done, otherwise print a custom message
nothing_done:
	@echo "Make done, but no tasks were performed."

# Include this phony target as a dependency to main targets if needed
