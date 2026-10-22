SUMMARY = "Recipe for building an external aximemorymap Linux kernel module"
SECTION = "Yocto/modules"

LICENSE = "BSD-3-Clause"
LICENSE_PATH += "${S}"

# Compute the LICENSE.txt checksum at parse time so the annual copyright
# year bump in LICENSE.txt does not require a matching recipe update.
python __anonymous() {
    import hashlib
    lic = d.expand("${FILE_DIRNAME}/files/LICENSE.txt")
    bb.parse.mark_dependency(d, lic)
    with open(lic, "rb") as f:
        md5 = hashlib.md5(f.read()).hexdigest()
    d.setVar("LIC_FILES_CHKSUM", "file://LICENSE.txt;md5=" + md5)
}

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
