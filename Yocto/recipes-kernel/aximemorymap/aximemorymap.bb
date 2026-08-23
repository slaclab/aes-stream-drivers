SUMMARY = "Recipe for building an external aximemorymap Linux kernel module"
SECTION = "Yocto/modules"

LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=d404d49cf288cc77c21e9bbfd062bb54"
LICENSE_PATH += "${S}"

inherit module

INHIBIT_PACKAGE_STRIP = "1"

SRC_URI = "file://Makefile \
           file://aximemorymap.c \
           file://aximemorymap.h \
           file://DmaDriver.h \
           file://LICENSE.txt \
          "

S = "${WORKDIR}"

# The inherit of module.bbclass will automatically name module packages with
# "kernel-module-" prefix as required by the oe-core build environment.
