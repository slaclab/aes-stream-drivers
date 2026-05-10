SUMMARY = "Recipe for building an external axistreamdma Linux kernel module"
SECTION = "Yocto/modules"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause;md5=550794465ba0ec5312d6919e203a55f9"

inherit module

INHIBIT_PACKAGE_STRIP = "1"

# Resolve the aes-stream-drivers Git version at recipe parse time so the
# axi_stream_dma kernel module reports the real upstream tag in
# /proc/axi_stream_dma_0 — matching the host data_dev driver semantic.
# This must run at parse time (not do_configure) because by the time
# do_configure runs, sources have been copied to WORKDIR and ${THISDIR}
# is no longer a path inside a git checkout from the build's perspective.
python __anonymous() {
    import subprocess
    repo = d.expand("${THISDIR}/../../..")
    def _git(args):
        try:
            return subprocess.check_output(
                ["git", "-C", repo] + args,
                stderr=subprocess.DEVNULL,
            ).decode().strip()
        except Exception:
            return ""
    tag = _git(["describe", "--tags"]) or _git(["rev-parse", "--short", "HEAD"]) or "emulator"
    if _git(["status", "--short", "-uno"]):
        tag += "-dirty"
    d.setVar("GITV", tag)
}

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
    unset CFLAGS CPPFLAGS CXXFLAGS LDFLAGS
    oe_runmake KERNEL_PATH=${STAGING_KERNEL_DIR}   \
           KERNEL_VERSION=${KERNEL_VERSION}    \
           CC="${KERNEL_CC}" LD="${KERNEL_LD}" \
           AR="${KERNEL_AR}" \
               O=${STAGING_KERNEL_BUILDDIR} \
           KBUILD_EXTRA_SYMBOLS="${KBUILD_EXTRA_SYMBOLS}" \
           DMA_TX_BUFF_COUNT=${DMA_TX_BUFF_COUNT} \
           DMA_RX_BUFF_COUNT=${DMA_RX_BUFF_COUNT} \
           DMA_BUFF_SIZE=${DMA_BUFF_SIZE} \
           GITV="${GITV}" \
           ${MAKE_TARGETS}
}

# The inherit of module.bbclass will automatically name module packages with
# "kernel-module-" prefix as required by the oe-core build environment.
