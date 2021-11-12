SUMMARY = "Recipe for  build an external axistreamdma Linux kernel module"
SECTION = "PETALINUX/modules"
LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://COPYING;md5=12f884d2ae1ff87c09e5b7ccc2c4ca7e"

inherit module

INHIBIT_PACKAGE_STRIP = "1"

SRC_URI = "file://Makefile \
           file://rce_top.h \
           file://axistreamdma.c \
	   file://dma_common.h \
	   file://dma_common.c \
	   file://dma_buffer.h \
	   file://dma_buffer.c \
	   file://axis_gen2.h \
	   file://axis_gen2.c \
	   file://axis_gen1.h \
	   file://axis_gen1.c \
	   file://AxisDriver.h \
	   file://DmaDriver.h \
	   file://COPYING \
          "

S = "${WORKDIR}"

# The inherit of module.bbclass will automatically name module packages with
# "kernel-module-" prefix as required by the oe-core build environment.
