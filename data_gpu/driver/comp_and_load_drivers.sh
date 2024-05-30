#!/bin/bash

# Provide defaults for CC
[ -z "$CC" ] && CC=gcc

# Function to check if the script is run with sudo
check_sudo() {
    if [ "$EUID" -ne 0 ]; then
        echo "Error: This script must be run with sudo." >&2
        exit 1
    fi
}
# Checks that GCC matches what the kernel was built with
check_gcc_version() {
    _GCC_VER="$($CC --version | grep -Eo "\s[0-9]+\.[0-9]+\.[0-9]+\s" | awk '{$1=$1};1')"
    if ! cat /proc/version | grep -Eoq "gcc version $_GCC_VER"; then
        echo "Error: GCC version 'gcc version $_GCC_VER' does not match what the kernel was built with: '$(cat /proc/version | grep -Eo "gcc version [0-9]+\.[0-9]+\.[0-9]+")'"
        echo "  You can specify an alternative compiler by setting the 'CC' environment variable"
        exit 1
    fi
}

# Check if the script is run with sudo
check_sudo

# Check that our GCC matches what the kernel was built with
check_gcc_version

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
