#!/bin/bash

# Function to find the latest Nvidia version directory
get_latest_nvidia_version() {
  # Get a list of all directories in /usr/src starting with "nvidia-"
  nvidia_dirs=($(ls -d /usr/src/nvidia-*))
  # Sort the directories by version number using natural sorting
  IFS=$'\n' sorted_dirs=($(sort -V <<< "${nvidia_dirs[@]}"))
  # Return the last element (assuming it's the latest version)
  echo "${sorted_dirs[${#sorted_dirs[@]} - 1]}"
}

# Define Nvidia path
NVIDIA_PATH=$(get_latest_nvidia_version)
echo "Using Nvidia path: $NVIDIA_PATH"

RET_DIR=$PWD

/usr/sbin/rmmod datagpu
/usr/sbin/rmmod nvidia-drm
/usr/sbin/rmmod nvidia-uvm
/usr/sbin/rmmod nvidia-modeset
/usr/sbin/rmmod nvidia

cd $NVIDIA_PATH

make CC=gcc-12

modprobe ecc
/usr/sbin/insmod nvidia.ko NVreg_OpenRmEnableUnsupportedGpus=1 NVreg_EnableStreamMemOPs=1
/usr/sbin/insmod nvidia-modeset.ko
/usr/sbin/insmod nvidia-uvm.ko
/usr/sbin/insmod nvidia-drm.ko modeset=1

cd $RET_DIR

make NVIDIA_DRIVERS=$NVIDIA_PATH
/usr/sbin/insmod datagpu.ko
