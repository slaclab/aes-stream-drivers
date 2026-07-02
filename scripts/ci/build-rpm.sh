#!/usr/bin/env bash
# Runs inside the target Rocky Linux container (repo mounted at /ws).
# Required env vars: DRIVER_TYPE, TARGET_ENV
set -euo pipefail

dnf install -y -q epel-release >/dev/null
dnf install -y -q make git tar rpm-build dkms >/dev/null

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
SRC="${PWD}/stage/dkms_source_tree"

PKG=$(sed -n "s/^PACKAGE_NAME=//p" "${SRC}/dkms.conf" | tr -d '"')
DVER=$(sed -n "s/^PACKAGE_VERSION=//p" "${SRC}/dkms.conf" | tr -d '"')
MVER=$(echo "${DVER}" | sed "s/^v//; s/-/./g")
echo "PKG=${PKG} DKMS_VERSION=${DVER} META_VERSION=${MVER}"

mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,BUILDROOT}

cat > ~/rpmbuild/SPECS/datadev.spec <<SPEC
Name:           ${PKG}
Version:        ${MVER}
Release:        1%{?dist}
Summary:        AES Stream Drivers (datadev) DKMS module
License:        SLAC
BuildArch:      noarch
Requires:       dkms

%description
DKMS source package for the ${PKG} kernel module, built for ${TARGET_ENV}.

%install
mkdir -p %{buildroot}/usr/src/${PKG}-${DVER}
cp -a ${SRC}/. %{buildroot}/usr/src/${PKG}-${DVER}/

%files
/usr/src/${PKG}-${DVER}

%post
dkms add -m ${PKG} -v ${DVER} || true
dkms build -m ${PKG} -v ${DVER}
dkms install -m ${PKG} -v ${DVER} --force

%preun
dkms remove -m ${PKG} -v ${DVER} --all || true
SPEC

rpmbuild -bb ~/rpmbuild/SPECS/datadev.spec
OUT="${PKG}-${MVER}-${TARGET_ENV}.noarch.rpm"
cp ~/rpmbuild/RPMS/noarch/*.rpm "./${OUT}"
echo "=== Built ${OUT} ==="
rpm -qpi "./${OUT}"
rpm -qpl "./${OUT}"
