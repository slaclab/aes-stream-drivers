#!/bin/bash

NVIDIA_PATH=/usr/src/nvidia-535.154.05/
RET_DIR=$PWD

/usr/sbin/rmmod datagpu
/usr/sbin/rmmod nvidia-drm
/usr/sbin/rmmod nvidia-uvm
/usr/sbin/rmmod nvidia-modeset
/usr/sbin/rmmod nvidia

cd $NVIDIA_PATH

make CC=gcc-12

/usr/sbin/insmod nvidia.ko NVreg_OpenRmEnableUnsupportedGpus=1
/usr/sbin/insmod nvidia-modeset.ko
/usr/sbin/insmod nvidia-uvm.ko
/usr/sbin/insmod nvidia-drm.ko modeset=1

cd $RET_DIR

make NVIDIA_DRIVERS=$NVIDIA_PATH
/usr/sbin/insmod datagpu.ko
