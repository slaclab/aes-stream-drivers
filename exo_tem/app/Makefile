# 
# ----------------------------------------------------------------------------
# Title      : TEM applications makefile
# ----------------------------------------------------------------------------
# File       : Makefile
# Author     : Ryan Herbst, rherbst@slac.stanford.edu
# Created    : 2016-08-08
# Last update: 2016-08-08
# ----------------------------------------------------------------------------
# Description:
# TEM applications makefile
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_driver package. It is subject to 
# the license terms in the LICENSE.txt file found in the top-level directory 
# of this distribution and at: 
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
# No part of the aes_stream_driver package, including this file, may be 
# copied, modified, propagated, or distributed except according to the terms 
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------
# 

# Variables
CC       := g++
DEF      :=
BIN      := ./bin
OBJ      := ./.obj
CFLAGS   := -Wall -I../../common/app_lib -I../../include/
LFLAGS   := -lpthread

# Generic Sources
LIB_DIR := $(PWD)/../../common/app_lib
LIB_SRC := $(wildcard $(LIB_DIR)/*.cpp)
LIB_HDR := $(wildcard $(LIB_DIR)/*.h)
LIB     := $(patsubst $(LIB_DIR)/%.cpp,$(OBJ)/%.o,$(LIB_SRC))

# Final Sources
SRC_DIR := $(PWD)/src
SRC_SRC := $(wildcard $(SRC_DIR)/*.cpp)
SRC     := $(patsubst $(SRC_DIR)/%.cpp,$(BIN)/%,$(SRC_SRC))

# Targets
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

