#!/bin/bash

# Check if the script is run with sudo
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run with sudo." >&2
    exit 1
fi

# Get the gcc that kernel was built with
version_content=$(cat /proc/version)
CC=$(echo "$version_content" | grep -oP 'x86_64-linux-gnu-gcc-\d+')
echo "CC: $CC"

# Define Nvidia path
output=$(find /usr -name nv-p2p.h 2>/dev/null)
NVIDIA_PATH=$(echo "$output" | grep -oP '^/usr/src/nvidia-\d+\.\d+\.\d+' | head -n 1)
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

# Go to nvidia path and build Nvidia driver
cd "$NVIDIA_PATH" || { echo "Error: Failed to change directory to $NVIDIA_PATH"; exit 1; }
make CC=$CC

if modinfo ecc >/dev/null 2>&1; then
    modprobe ecc || { echo "Error: Failed to insert ecc module."; exit 1; }
fi

# Load the nvidia kernel drivers
/usr/sbin/insmod $NVIDIA_PATH/nvidia.ko NVreg_OpenRmEnableUnsupportedGpus=1 NVreg_EnableStreamMemOPs=1 || { echo "Error: Failed to insert nvidia.ko."; exit 1; }
/usr/sbin/insmod $NVIDIA_PATH/nvidia-modeset.ko || { echo "Error: Failed to insert nvidia-modeset.ko."; exit 1; }
/usr/sbin/insmod $NVIDIA_PATH/nvidia-drm.ko modeset=1 || { echo "Error: Failed to insert nvidia-drm.ko."; exit 1; }
/usr/sbin/insmod $NVIDIA_PATH/nvidia-uvm.ko || { echo "Error: Failed to insert nvidia-uvm.ko."; exit 1; }

# Go to nvidia path and build Nvidia driver
cd "$RET_DIR" || { echo "Error: Failed to change directory to $RET_DIR"; exit 1; }
make CC=$CC NVIDIA_DRIVERS=$NVIDIA_PATH
/usr/sbin/insmod $RET_DIR/datagpu.ko || { echo "Error: Failed to insert datagpu.ko."; exit 1; }

