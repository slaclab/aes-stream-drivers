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

# Determine the NVIDIA module version installed for this kernel
NVIDIA_VER=$(dkms status nvidia -k $VERSION | tr '/' '-' | awk -F, '{ print $1 }')

# Generate a local Makefile that contains the configuration
echo "NVIDIA_DRIVERS=$SRCTREE/$NVIDIA_VER" > Makefile.local

echo "--> Building NVIDIA drivers version $NVIDIA_VER"

make -C "$SRCTREE/$NVIDIA_VER" -j$(nproc)
