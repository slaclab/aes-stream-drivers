SUMMARY = "Recipe for building AXI stream DMA sample/test applications"
SECTION = "Yocto/modules"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause;md5=550794465ba0ec5312d6919e203a55f9"

SRC_URI = "file://Makefile          \
           file://AxisDriver.h      \
           file://AxiVersion.h      \
           file://AppUtils.h        \
           file://DmaDriver.h       \
           file://DataDriver.h      \
           file://dmaLoopTest.cpp   \
           file://dmaSetDebug.cpp   \
           file://dmaWrite.cpp      \
           file://dmaRead.cpp       \
           file://PrbsData.cpp      \
           file://PrbsData.h        \
        "

S = "${WORKDIR}"

do_compile() {
    pwd
    oe_runmake
}

do_install() {
    oe_runmake install PREFIX="${D}/usr/local"
}

FILES:${PN} += "/usr/local/bin/axiStreamLoop"
FILES:${PN} += "/usr/local/bin/axiStreamWrite"
FILES:${PN} += "/usr/local/bin/axiStreamRead"
FILES:${PN} += "/usr/local/bin/axiStreamSetDebug"
