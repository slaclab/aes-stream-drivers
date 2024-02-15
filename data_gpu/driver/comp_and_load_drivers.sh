#!/bin/bash

NVIDIA_PATH=/usr/src/nvidia-535.154.05/
RET_DIR=$PWD

sudo /usr/sbin/rmmod datagpu
sudo /usr/sbin/rmmod nvidia-drm
sudo /usr/sbin/rmmod nvidia-uvm
sudo /usr/sbin/rmmod nvidia-modeset
sudo /usr/sbin/rmmod nvidia

cd $NVIDIA_PATH

make CC=gcc-12

sudo /usr/sbin/insmod nvidia.ko NVreg_OpenRmEnableUnsupportedGpus=1
sudo /usr/sbin/insmod nvidia-modeset.ko
sudo /usr/sbin/insmod nvidia-uvm.ko
sudo /usr/sbin/insmod nvidia-drm.ko modeset=1

cd $RET_DIR

make NVIDIA_DRIVERS=$NVIDIA_PATH
sudo /usr/sbin/insmod datagpu.ko
