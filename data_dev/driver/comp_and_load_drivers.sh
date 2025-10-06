#!/bin/bash
# vi: et sw=4 ts=4

# Check if the script is run with sudo
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run with sudo." >&2
    exit 1
fi

# Change directory to where this script is
cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1

# Determine the Linux distribution
if [ -f /etc/os-release ]; then
    . /etc/os-release
    distro=$ID
else
    echo "Error: Cannot determine the Linux distribution." >&2
    exit 1
fi

# Set the CC variable based on the distribution
if [ "$distro" == "ubuntu" ] || [ "$distro" == "debian" ]; then
    # Get the gcc that kernel was built with
    version_content=$(cat /proc/version)
    CC=$(echo "$version_content" | grep -oP 'x86_64-linux-gnu-gcc-\d+')
elif [ "$distro" == "rhel" ] || [ "$distro" == "rocky" ]; then
    CC="x86_64-redhat-linux-gcc"
else
    echo "Error: Unsupported Linux distribution." >&2
    exit 1
fi
echo "CC: $CC"

# Define Nvidia path
output=$(find /usr -name nv-p2p.h 2>/dev/null)
if [ -z "$NVIDIA_PATH" ]; then
    NVIDIA_PATH=$(echo "$output" | grep -oP '^/usr/src/nvidia(-open)?-\d+\.\d+\.\d+' | head -n 1)
fi
if [ -z "$NVIDIA_PATH" ]; then
    echo "Could not find NVIDIA open drivers in /usr/src. Is it installed?"
    exit 1
fi
echo "Using Nvidia path: $NVIDIA_PATH"

# Return directory
RET_DIR=$PWD
echo "Using RET_DIR: $RET_DIR"

# Remove existing Nvidia modules (if any)
modules=("datadev" "nvidia_fs" "nvidia-drm" "nvidia-uvm" "nvidia-modeset" "nvidia" "nouveau")
for module in "${modules[@]}"; do
    output=$(/usr/sbin/rmmod $module 2>&1)
    status=$?
    if [[ $status -ne 0 ]]; then
        if [[ $output == *"Module $module is in use"* ]]; then
            echo "Error: Module $module is in use. Stopping script."
            exit 1
        fi
    fi
done

# Go to nvidia path and build Nvidia driver
cd "$NVIDIA_PATH" || { echo "Error: Failed to change directory to $NVIDIA_PATH"; exit 1; }
# Clean previous builds
make clean
# Build Nvidia driver
make CC=$CC -j $(nproc)

DEPS="ecc video"
for MOD in $DEPS; do
    if modinfo $MOD >/dev/null 2>&1; then
        modprobe $MOD || { echo "Error: Failed to insert $MOD module."; exit 1; }
    fi
done

if [ -f "$NVIDIA_PATH/nvidia.ko" ]; then
    DRIVER_PATH="$NVIDIA_PATH"
elif [ -f "$NVIDIA_PATH/kernel-open/nvidia.ko" ]; then
    DRIVER_PATH="$NVIDIA_PATH/kernel-open"
else
    echo "ERROR: Unable to determine location of nvidia.ko!"
    exit 1
fi

# Load the nvidia kernel drivers
/usr/sbin/insmod $DRIVER_PATH/nvidia.ko NVreg_OpenRmEnableUnsupportedGpus=1 NVreg_EnableStreamMemOPs=1 || { echo "Error: Failed to insert nvidia.ko."; exit 1; }
/usr/sbin/insmod $DRIVER_PATH/nvidia-modeset.ko || { echo "Error: Failed to insert nvidia-modeset.ko."; exit 1; }
/usr/sbin/insmod $DRIVER_PATH/nvidia-drm.ko modeset=1 || { echo "Error: Failed to insert nvidia-drm.ko."; exit 1; }
/usr/sbin/insmod $DRIVER_PATH/nvidia-uvm.ko || { echo "Error: Failed to insert nvidia-uvm.ko."; exit 1; }

# Go to nvidia path and build Nvidia driver
cd "$RET_DIR" || { echo "Error: Failed to change directory to $RET_DIR"; exit 1; }
# Clean previous builds
make clean
# Build datadev driver
make CC=$CC NVIDIA_DRIVERS=$NVIDIA_PATH
/usr/sbin/insmod $RET_DIR/datadev.ko || { echo "Error: Failed to insert datadev.ko."; exit 1; }
