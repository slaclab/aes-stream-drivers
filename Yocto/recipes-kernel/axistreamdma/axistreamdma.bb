SUMMARY = "Recipe for  build an external axistreamdma Linux kernel module"
SECTION = "Yocto/modules"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause;md5=550794465ba0ec5312d6919e203a55f9"

inherit module

INHIBIT_PACKAGE_STRIP = "1"

SRC_URI = "file://Makefile \
           file://axistreamdma.h \
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

do_compile() {
    oe_runmake KERNEL_SRC=${STAGING_KERNEL_DIR} DMA_TX_BUFF_COUNT=${DMA_TX_BUFF_COUNT} DMA_RX_BUFF_COUNT=${DMA_RX_BUFF_COUNT} DMA_BUFF_SIZE=${DMA_BUFF_SIZE}
}

# The inherit of module.bbclass will automatically name module packages with
# "kernel-module-" prefix as required by the oe-core build environment.
