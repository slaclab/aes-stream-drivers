#!/bin/bash

# Function to check if gcc-12 is installed
check_gcc_12_installed() {
    if ! command -v gcc-12 >/dev/null 2>&1; then
        echo "Error: gcc-12 is not installed. Please install gcc-12 and try again." >&2
        exit 1
    fi
}

# Call the gcc-12 check function early in the script to ensure it's available
check_gcc_12_installed

# Function to find the latest Nvidia version directory
get_latest_nvidia_path() {
    # Navigate to the /usr/src directory
    cd /usr/src

    # List and sort NVIDIA directories, then get the last one (the latest)
    latest_nvidia_path=$(ls -d nvidia-* | sort -V | tail -n 1)

    # Check if no NVIDIA directory was found
    if [ -z "$latest_nvidia_path" ]; then
        echo "Error: No NVIDIA directory found in /usr/src" >&2
        exit 1
    else
        # Print the full path of the latest NVIDIA directory
        echo "/usr/src/$latest_nvidia_path"
    fi
}

# Return directory
RET_DIR=$PWD
echo "Using RET_DIR: $RET_DIR"

# Remove existing Nvidia modules (if any)
/usr/sbin/rmmod datagpu 2>/dev/null
/usr/sbin/rmmod nvidia-drm 2>/dev/null
/usr/sbin/rmmod nvidia-uvm 2>/dev/null
/usr/sbin/rmmod nvidia-modeset 2>/dev/null
/usr/sbin/rmmod nvidia 2>/dev/null

# Define Nvidia path
NVIDIA_PATH=$(get_latest_nvidia_path)
echo "Using Nvidia path: $NVIDIA_PATH"

cd $NVIDIA_PATH

make CC=gcc-12

modprobe ecc || { echo "Error: Failed to insert ecc module."; exit 1; }

/usr/sbin/insmod nvidia.ko NVreg_OpenRmEnableUnsupportedGpus=1 NVreg_EnableStreamMemOPs=1 || { echo "Error: Failed to insert nvidia.ko."; exit 1; }

/usr/sbin/insmod nvidia-modeset.ko || { echo "Error: Failed to insert nvidia-modeset.ko."; exit 1; }

/usr/sbin/insmod nvidia-uvm.ko || { echo "Error: Failed to insert nvidia-uvm.ko."; exit 1; }

/usr/sbin/insmod nvidia-drm.ko modeset=1 || { echo "Error: Failed to insert nvidia-drm.ko."; exit 1; }

cd $RET_DIR

make NVIDIA_DRIVERS=$NVIDIA_PATH
/usr/sbin/insmod datagpu.ko || { echo "Error: Failed to insert datagpu.ko."; exit 1; }
