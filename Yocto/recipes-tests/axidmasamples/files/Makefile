# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description : Slightly special Makefile for the applications built under
#  				Yocto. Includes an install target.
# ----------------------------------------------------------------------------
# This file is part of the aes_stream_drivers package. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the aes_stream_drivers package, including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------

CPPFLAGS	+= -DDEFAULT_AXI_DEVICE=\"/dev/axi_stream_dma_0\" -I. -isystem.
CFLAGS		+= -O2 -g
CXXFLAGS	+= -O2 -g $(CPPFLAGS)
LDFLAGS		+= -pthread
PREFIX		?= /usr/local
APPS		:= axiStreamLoop axiStreamWrite axiStreamSetDebug axiStreamRead

all: $(APPS)

install: $(APPS)
	@echo "Installing $@ to $(PREFIX)"
	@mkdir -p $(PREFIX)/bin/
	$(foreach app,$(APPS), \
		install -m 0777 $(app) $(PREFIX)/bin/;)

clean:
	$(foreach app,$(APPS), \
		rm -fv $(app);)

axiStreamLoop: dmaLoopTest.cpp PrbsData.cpp
	@echo Building $@...
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

axiStreamWrite: dmaWrite.cpp PrbsData.cpp
	@echo Building $@...
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

axiStreamRead: dmaRead.cpp PrbsData.cpp
	@echo Building $@...
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

axiStreamSetDebug: dmaSetDebug.cpp
	@echo Building $@...
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

