# GPU Enabled Driver

To build this driver you need to have the NVIDA Open GPU Kernel Modules installed. This driver will not compile gainst the CUDA toolkit drivers.

https://docs.nvidia.com/cuda/cuda-installation-guide-linux/index.html

See section 5.

A script in this directory 'comp_and_load_drivers.sh' is provided to compile and load the nvidia drivers as well as the driver in this directory. Edit this file and update the NVIDIA_PATH value at the top to the install directory for the nvidia drivers.

```bash
$ sudo apt-get install nvidia-kernel-source-545-open
$ sudo apt-get install nvidia-cuda-toolkit
$ sudo ./comp_and_load_drivers.sh
```

If NVLink support is required, install cuda-drivers-fabricmanager-<driver version> as well. However, as of Febuary 2024, the latest version of fabricmanager is 535.
When it becomes available on Ubuntu, install with:

```
$ sudo apt-get install cuda-drivers-fabricmanager-545
```

