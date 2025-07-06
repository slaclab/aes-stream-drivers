SUMMARY = "Recipe for  build an external axistreamdma Linux kernel module"
SECTION = "PETALINUX/modules"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

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
          "

S = "${WORKDIR}"

# The inherit of module.bbclass will automatically name module packages with
# "kernel-module-" prefix as required by the oe-core build environment.
