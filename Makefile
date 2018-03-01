# 
# ----------------------------------------------------------------------------
# Title      : drivers makefile
# ----------------------------------------------------------------------------
# File       : Makefile
# Created    : 2016-08-08
# ----------------------------------------------------------------------------
# Description:
# drivers makefile
# ----------------------------------------------------------------------------
# This file is part of the rogue software package. It is subject to 
# the license terms in the LICENSE.txt file found in the top-level directory 
# of this distribution and at: 
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
# No part of the rogue software package, including this file, may be 
# copied, modified, propagated, or distributed except according to the terms 
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------
# 
MAKE_HOME := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

LOC_VERS := $(shell ls /lib/modules/)
RCE_DIRS := /afs/slac/g/cci/volumes/vol1/xilinx/linux-xlnx-v2015.2.03
RCE_DIRS += /afs/slac/g/cci/volumes/vol1/xilinx/linux-xlnx-v2016.4
RCE_DIRS += /afs/slac/g/cci/volumes/vol1/xilinx/backup/linux-xlnx-v2016.1.01

all: 
	@echo "Options: app driver rce"

app:
	@make -C $(MAKE_HOME)/exo_tem/app clean
	@make -C $(MAKE_HOME)/exo_tem/app
	@make -C $(MAKE_HOME)/pgpcard/app clean
	@make -C $(MAKE_HOME)/pgpcard/app
	@make -C $(MAKE_HOME)/rce_stream/app clean
	@make -C $(MAKE_HOME)/rce_stream/app
	@make -C $(MAKE_HOME)/data_dev/app clean
	@make -C $(MAKE_HOME)/data_dev/app

driver:
	@mkdir -p $(MAKE_HOME)/install;
	@ $(foreach ver,$(LOC_VERS), \
		mkdir -p $(MAKE_HOME)/install/$(ver); \
		make -C $(MAKE_HOME)/exo_tem/driver KVER=$(ver) clean; \
		make -C $(MAKE_HOME)/exo_tem/driver KVER=$(ver); \
		scp $(MAKE_HOME)/exo_tem/driver/*.ko $(MAKE_HOME)/install/$(ver); \
		make -C $(MAKE_HOME)/pgpcard/driver KVER=$(ver) clean; \
		make -C $(MAKE_HOME)/pgpcard/driver KVER=$(ver); \
		scp $(MAKE_HOME)/pgpcard/driver/*.ko $(MAKE_HOME)/install/$(ver); \
		make -C $(MAKE_HOME)/data_dev/driver KVER=$(ver) clean; \
		make -C $(MAKE_HOME)/data_dev/driver KVER=$(ver); \
		scp $(MAKE_HOME)/data_dev/driver/*.ko $(MAKE_HOME)/install/$(ver); \
	)

rce:
	@mkdir -p $(MAKE_HOME)/install;
	@ $(foreach d,$(RCE_DIRS), \
		mkdir -p $(MAKE_HOME)/install/$(shell make -C $(d) -s kernelversion).arm; \
		make -C $(MAKE_HOME)/rce_stream/driver KDIR=$(d) clean; \
		make -C $(MAKE_HOME)/rce_stream/driver KDIR=$(d); \
		scp $(MAKE_HOME)/rce_stream/driver/*.ko $(MAKE_HOME)/install/$(shell make -C $(d) -s kernelversion).arm/; \
	)

