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

Disable the Xserver and nvidia-persistenced to prevent rmmod due to Module XXX is in use by: YYY
because the Nvidia driver gets loaded by default at startup 

https://forums.developer.nvidia.com/t/cant-install-new-driver-cannot-unload-module/63639

```bash
$ sudo systemctl disable gdm     # For GNOME Display Manager
$ sudo systemctl disable lightdm # For LightDM
$ sudo systemctl disable sddm    # For SDDM
$ sudo systemctl disable nvidia-persistenced
```
