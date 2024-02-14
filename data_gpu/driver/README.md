# GPU Enabled Driver

To build this driver you need to have the NVIDA Open GPU Kernel Modules installed. This driver will not compile gainst the CUDA toolkit drivers.

https://docs.nvidia.com/cuda/cuda-installation-guide-linux/index.html

See section 5.

The nvidia instructions are a bit off. These are the current steps taken at the time of this readme update:

```bash
$ apt-get install nvidia-kernel-source-535-open
$ apt-get install cuda-drivers-fabricmanager-535
$ cd /usr/src/nvidia-535.154.05
$ make CC=gcc-12
$ insmod nvidia.ko
```

You can then build the data_gpu image.

Update the makefile and edit the following line to point to the correct path to the nvida drivers:

NVIDIA_DRIVERS := /usr/src/nvidia-535.154.05/nvidia

```bash
$ make
$ insmod data_gpu.ko
```
