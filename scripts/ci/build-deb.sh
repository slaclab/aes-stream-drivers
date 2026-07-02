#!/usr/bin/env bash
# Runs inside the target Ubuntu container (repo mounted at /ws).
# Required env vars: DRIVER_TYPE, TARGET_ENV
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq make git tar dpkg-dev dkms >/dev/null

make -C data_dev/driver dkms
cd data_dev/driver

if [ "${DRIVER_TYPE}" = "gpu" ]; then
    TARBALL=$(ls datadev-gpu-dkms-*.tar.gz 2>/dev/null | head -n1)
else
    TARBALL=$(ls datadev-dkms-*.tar.gz 2>/dev/null | head -n1)
fi
if [ -z "${TARBALL}" ]; then
    echo "ERROR: DKMS source tarball not produced for ${DRIVER_TYPE}" >&2
    exit 1
fi
echo "Using tarball: ${TARBALL}"

rm -rf stage && mkdir -p stage
tar -xzf "${TARBALL}" -C stage
SRC=stage/dkms_source_tree

PKG=$(sed -n "s/^PACKAGE_NAME=//p" "${SRC}/dkms.conf" | tr -d '"')
DVER=$(sed -n "s/^PACKAGE_VERSION=//p" "${SRC}/dkms.conf" | tr -d '"')
MVER=$(echo "${DVER}" | sed "s/^v//; s/-/./g")
echo "PKG=${PKG} DKMS_VERSION=${DVER} META_VERSION=${MVER}"

ROOT=debpkg
rm -rf "${ROOT}"
install -d "${ROOT}/usr/src/${PKG}-${DVER}"
cp -a "${SRC}/." "${ROOT}/usr/src/${PKG}-${DVER}/"
install -d "${ROOT}/DEBIAN"

cat > "${ROOT}/DEBIAN/control" <<CTL
Package: ${PKG}
Version: ${MVER}
Section: kernel
Priority: optional
Architecture: all
Maintainer: SLAC CI <ci@slac.stanford.edu>
Depends: dkms, build-essential, linux-headers-generic
Description: AES Stream Drivers (datadev) DKMS module
 DKMS source package for the ${PKG} kernel module, built for ${TARGET_ENV}.
CTL

cat > "${ROOT}/DEBIAN/postinst" <<POST
#!/bin/sh
set -e
dkms add -m ${PKG} -v ${DVER} || true
dkms build -m ${PKG} -v ${DVER}
dkms install -m ${PKG} -v ${DVER} --force
POST

cat > "${ROOT}/DEBIAN/prerm" <<PRERM
#!/bin/sh
set -e
dkms remove -m ${PKG} -v ${DVER} --all || true
PRERM

chmod 0755 "${ROOT}/DEBIAN/postinst" "${ROOT}/DEBIAN/prerm"

OUT="${PKG}_${MVER}_${TARGET_ENV}_all.deb"
dpkg-deb --build --root-owner-group "${ROOT}" "${OUT}"
echo "=== Built ${OUT} ==="
dpkg-deb -I "${OUT}"
dpkg-deb -c "${OUT}"
