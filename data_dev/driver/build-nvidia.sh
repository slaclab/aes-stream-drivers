#!/usr/bin/env bash

set -e

while test $# -gt 0; do
    case $1 in
        -v)
            VERSION="$2"
            shift 2
            ;;
        -s)
            SRCTREE="$2"
            shift 2
            ;;
        *)
            echo "USAGE: $0 -s src_tree -v kver"
            exit 1
            ;;
    esac
done

if [ $EUID -ne 0 ]; then
    echo "This script must be run as root!"
    exit 1
fi

if [ -z "$VERSION" ] || [ -z "$SRCTREE" ]; then
    echo "USAGE: $0 -s src_tree -v kver"
    exit 1
fi

# Determine the NVIDIA module version installed for this kernel.
# On CI/emulator systems the real NVIDIA driver is absent, so `dkms status
# nvidia` returns empty.  Building with NVIDIA_DRIVERS unset exercises the
# tarball/install path without a hard NVIDIA dependency, but it also leaves
# DATA_GPU undefined, so the resulting module reports
# 'GPUAsync Support : Disabled' and cannot do GPU DMA at all -- despite being
# installed under the name datadev-gpu-dkms.  Nothing downstream distinguishes
# the two builds: both are datadev.ko with the same module name, so lsmod and
# modinfo look identical and only /proc/datadev_* tells them apart.
#
# That is fine for CI and useless on a real GPU node, so it has to be asked for
# rather than fallen into.  Set ALLOW_NO_NVIDIA=1 to opt in.
NVIDIA_VER=$(dkms status nvidia -k "$VERSION" 2>/dev/null | tr '/' '-' | awk -F, '{ print $1 }')

if [ -z "$NVIDIA_VER" ] || [ ! -d "$SRCTREE/$NVIDIA_VER" ]; then
    if [ "${ALLOW_NO_NVIDIA:-0}" != "1" ]; then
        echo "ERROR: no NVIDIA DKMS module found for kernel $VERSION."       >&2
        echo "       Looked for: dkms status nvidia -k $VERSION"             >&2
        echo "                   $SRCTREE/<nvidia-version>/"                 >&2
        echo "       Continuing would build datadev-gpu WITHOUT DATA_GPU,"   >&2
        echo "       producing a module that cannot do GPU DMA while still"  >&2
        echo "       being named datadev-gpu.  Install and register the"     >&2
        echo "       NVIDIA open kernel modules for this kernel first."      >&2
        echo "       To build without GPU support anyway, as CI does,"       >&2
        echo "       set ALLOW_NO_NVIDIA=1."                                 >&2
        exit 1
    fi
    echo "--> ALLOW_NO_NVIDIA=1: no NVIDIA DKMS module for kernel $VERSION;"
    echo "    building datadev-gpu WITHOUT GPU support.  The module will"
    echo "    report 'GPUAsync Support : Disabled'."
    : > Makefile.local
    exit 0
fi

# Generate a local Makefile that contains the configuration
echo "NVIDIA_DRIVERS=$SRCTREE/$NVIDIA_VER" > Makefile.local

echo "--> Building NVIDIA drivers version $NVIDIA_VER"

make -C "$SRCTREE/$NVIDIA_VER" -j$(nproc)
