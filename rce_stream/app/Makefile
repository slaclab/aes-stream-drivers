# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description:
# PGP applications makefile
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
CC       := g++
DEF      :=
BIN      := $(HOME)/bin
OBJ      := $(HOME)/.obj
CFLAGS   := -Wall -I$(HOME)/../../common/app_lib -I$(HOME)/../../include/
CFLAGS32 := -Wno-format
LFLAGS   := -lpthread

# Generic Sources
LIB_DIR := $(HOME)/../../common/app_lib
LIB_SRC := $(wildcard $(LIB_DIR)/*.cpp)
LIB_HDR := $(wildcard $(LIB_DIR)/*.h)
LIB     := $(patsubst $(LIB_DIR)/%.cpp,$(OBJ)/%.o,$(LIB_SRC))

# Final Sources
SRC_DIR := $(HOME)/src
SRC_SRC := $(wildcard $(SRC_DIR)/*.cpp)
SRC     := $(patsubst $(SRC_DIR)/%.cpp,$(BIN)/%,$(SRC_SRC))

# Default
all: $(LIB) $(SRC)

# Clean
clean:
	@rm -f $(BIN)/*
	@rm -f $(OBJ)/*

# Compile Generic Sources
$(OBJ)/%.o: $(LIB_DIR)/%.cpp $(LIB_DIR)/%.h
	@test -d $(OBJ) || mkdir $(OBJ)
	@echo "Compiling $@"; $(CC) -c $(CFLAGS) $(CFLAGS32) $(DEF) -o $@ $<

$(BIN)/%: $(SRC_DIR)/%.cpp $(LIB) 
	@test -d $(BIN) || mkdir $(BIN)
	@echo "Compiling $@"; $(CC) $(CFLAGS) $(CFLAGS32) $(DEF) $(OBJ)/* -o $@ $< $(LFLAGS)

