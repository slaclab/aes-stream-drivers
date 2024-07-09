#!/bin/bash

# Check if the script is run with sudo
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run with sudo." >&2
    exit 1
fi

# Get the gcc that kernel was built with
version_info=$(cat /proc/version)
CC=$(echo "$version_info" | grep -oP '\b\w+-\w+-gcc-\d+\b')
echo "CC: $CC"

# Define Nvidia path
output=$(find /usr -name nv-p2p.h)
NVIDIA_PATH=$(echo "$output" | grep -oP '^/usr/src/nvidia-\d+\.\d+\.\d+')
echo "Using Nvidia path: $NVIDIA_PATH"

# Return directory
RET_DIR=$PWD
echo "Using RET_DIR: $RET_DIR"

# Remove existing Nvidia modules (if any)
/usr/sbin/rmmod datagpu 2>/dev/null
/usr/sbin/rmmod nvidia-drm 2>/dev/null
/usr/sbin/rmmod nvidia-uvm 2>/dev/null
/usr/sbin/rmmod nvidia-modeset 2>/dev/null
/usr/sbin/rmmod nvidia 2>/dev/null

# Go to nvidia path and build
cd $NVIDIA_PATH
make

if modinfo ecc >/dev/null 2>&1; then
    modprobe ecc || { echo "Error: Failed to insert ecc module."; exit 1; }
fi

/usr/sbin/insmod nvidia.ko NVreg_OpenRmEnableUnsupportedGpus=1 NVreg_EnableStreamMemOPs=1 || { echo "Error: Failed to insert nvidia.ko."; exit 1; }

/usr/sbin/insmod nvidia-modeset.ko || { echo "Error: Failed to insert nvidia-modeset.ko."; exit 1; }

/usr/sbin/insmod nvidia-uvm.ko || { echo "Error: Failed to insert nvidia-uvm.ko."; exit 1; }

/usr/sbin/insmod nvidia-drm.ko modeset=1 || { echo "Error: Failed to insert nvidia-drm.ko."; exit 1; }

cd $RET_DIR

make NVIDIA_DRIVERS=$NVIDIA_PATH
/usr/sbin/insmod datagpu.ko || { echo "Error: Failed to insert datagpu.ko."; exit 1; }
