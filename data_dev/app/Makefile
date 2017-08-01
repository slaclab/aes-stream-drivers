# 
# ----------------------------------------------------------------------------
# Title      : Data applications makefile
# ----------------------------------------------------------------------------
# File       : Makefile
# Author     : Ryan Herbst, rherbst@slac.stanford.edu
# Created    : 2016-08-08
# Last update: 2016-08-08
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

# Variables
HOME     := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
CC       := g++
DEF      :=
BIN      := $(HOME)/bin
OBJ      := $(HOME)/.obj
CFLAGS   := -Wall -I$(HOME)/../../include/ -I$(HOME)/../../common/app_lib/
LFLAGS   := -lpthread

# Generic Sources
LIB_DIR := $(HOME)/../../common/app_lib/
LIB_SRC := $(wildcard $(LIB_DIR)/*.cpp)
LIB_HDR := $(wildcard $(LIB_DIR)/*.h)
LIB_OBJ := $(patsubst $(LIB_DIR)/%.cpp,$(OBJ)/%.o,$(LIB_SRC))

# Application Sources
APP_DIR := $(HOME)/src
APP_SRC := $(wildcard $(APP_DIR)/*.cpp)
APP_BIN := $(patsubst $(APP_DIR)/%.cpp,$(BIN)/%,$(APP_SRC))

# Targets
all: $(LIB_OBJ) $(APP_BIN) 

# Clean
clean:
	@rm -f $(BIN)/*
	@rm -f $(OBJ)/*

# Compile Generic Sources
$(OBJ)/%.o: $(LIB_DIR)/%.cpp $(LIB_DIR)/%.h
	@test -d $(OBJ) || mkdir $(OBJ)
	@echo "Compiling $@ from $<"; $(CC) -c $(CFLAGS) $(DEF) -o $@ $<

# Application sources
$(BIN)/%: $(APP_DIR)/%.cpp $(LIB_OBJ)
	@test -d $(BIN) || mkdir $(BIN)
	@echo "Compiling $@ from $<"; $(CC) $(CFLAGS) $(CFLAGS32) $(DEF) $(LIB_OBJ) -o $@ $< $(LFLAGS)

