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
# nvidia` returns empty.  Skip the NVIDIA rebuild in that case: DKMS then
# builds datadev-gpu with NVIDIA_DRIVERS unset, which exercises the
# tarball/install path without a hard NVIDIA dependency.
NVIDIA_VER=$(dkms status nvidia -k "$VERSION" 2>/dev/null | tr '/' '-' | awk -F, '{ print $1 }')

if [ -z "$NVIDIA_VER" ] || [ ! -d "$SRCTREE/$NVIDIA_VER" ]; then
    echo "--> No NVIDIA DKMS module found for kernel $VERSION; skipping NVIDIA symbol rebuild."
    : > Makefile.local
    exit 0
fi

# Generate a local Makefile that contains the configuration
echo "NVIDIA_DRIVERS=$SRCTREE/$NVIDIA_VER" > Makefile.local

echo "--> Building NVIDIA drivers version $NVIDIA_VER"

make -C "$SRCTREE/$NVIDIA_VER" -j$(nproc)
