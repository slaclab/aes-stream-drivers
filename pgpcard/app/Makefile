#
# ----------------------------------------------------------------------------
# Title      : PGP applications makefile
# ----------------------------------------------------------------------------
# File       : Makefile
# Author     : Ryan Herbst, rherbst@slac.stanford.edu
# Created    : 2016-08-08
# Last update: 2016-08-08
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
#

# Variables
HOME     := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
CC       := $(CROSS_COMPILE)g++
DEF      :=
BIN      := $(HOME)/bin
OBJ      := $(HOME)/.obj
CFLAGS   := -Wall -I$(HOME)/../../include/ -I$(HOME)/../../common/app_lib/
CFLAGS   += -I$(HOME)/../include/ -I$(HOME)/../app_lib/
LFLAGS   := -lpthread

# Generic Sources
LIB_DIR := $(HOME)/../../common/app_lib/
LIB_SRC := $(wildcard $(LIB_DIR)/*.cpp)
LIB_HDR := $(wildcard $(LIB_DIR)/*.h)
LIB_OBJ := $(patsubst $(LIB_DIR)/%.cpp,$(OBJ)/%.o,$(LIB_SRC))

# PGP-local library sources. Kept here rather than in common/app_lib so that
# retiring this driver does not require touching shared code. PciCardProm was
# deliberately removed from common/app_lib because PROM flavors are too varied
# to support generically; the two PGP PROM applications still need it.
PGP_LIB_DIR := $(HOME)/../app_lib/
PGP_LIB_SRC := $(wildcard $(PGP_LIB_DIR)/*.cpp)
PGP_LIB_HDR := $(wildcard $(PGP_LIB_DIR)/*.h)
PGP_LIB_OBJ := $(patsubst $(PGP_LIB_DIR)/%.cpp,$(OBJ)/%.o,$(PGP_LIB_SRC))

ALL_LIB_OBJ := $(LIB_OBJ) $(PGP_LIB_OBJ)

# Application Sources
APP_DIR := $(HOME)/src
APP_SRC := $(wildcard $(APP_DIR)/*.cpp)
APP_BIN := $(patsubst $(APP_DIR)/%.cpp,$(BIN)/%,$(APP_SRC))

# Targets
.PHONY: all clean

all: $(ALL_LIB_OBJ) $(APP_BIN)

# Clean
clean:
	@rm -f $(BIN)/*
	@rm -f $(OBJ)/*

# Compile Generic Sources
$(OBJ)/%.o: $(LIB_DIR)/%.cpp $(LIB_DIR)/%.h
	@test -d $(OBJ) || mkdir $(OBJ)
	@echo "Compiling $@ from $<"; $(CC) -c $(CFLAGS) $(DEF) -o $@ $<

# Compile PGP-local library sources
$(OBJ)/%.o: $(PGP_LIB_DIR)/%.cpp $(PGP_LIB_DIR)/%.h
	@test -d $(OBJ) || mkdir $(OBJ)
	@echo "Compiling $@ from $<"; $(CC) -c $(CFLAGS) $(DEF) -o $@ $<

# Application sources
$(BIN)/%: $(APP_DIR)/%.cpp $(ALL_LIB_OBJ)
	@test -d $(BIN) || mkdir $(BIN)
	@echo "Compiling $@ from $<"; $(CC) $(CFLAGS) $(CFLAGS32) $(DEF) $(ALL_LIB_OBJ) -o $@ $< $(LFLAGS)

