# How to cross-compile the kernel driver

To cross-compile the kernel driver you need to define the `ARCH` and `CROSS_COMPILE` variables when calling `make`. Also, you need to point `KERNELDIR` to the location of the kernel sources.

For example, to cross-compile the driver for the SLAC buildroot `2019.08` version for the `x86_64` architecture, you should call `make` this way:

```bash
$ make \
ARCH=x86_64 \
CROSS_COMPILE=/sdf/sw/epics/package/linuxRT/buildroot-2019.08/host/linux-x86_64/x86_64/usr/bin/x86_64-buildroot-linux-gnu- \
KERNELDIR=/sdf/sw/epics/package/linuxRT/buildroot-2019.08/buildroot-2019.08-x86_64/output/build/linux-4.14.139
```

On the other hand, if you do not want to cross-compile the driver, and build it for the host instead, you need to call `make` without defining any variable:

```bash
$ make
```

# GPU Enabled Driver

To build this driver with GPU Async support you need to have the NVIDA Open GPU Kernel Modules installed. This driver will not compile against the CUDA toolkit drivers.

https://docs.nvidia.com/cuda/cuda-installation-guide-linux/index.html

See section 5.

<!--- ######################################################## -->

## System Configuration

Disable the Xserver and nvidia-persistenced to prevent rmmod due to Module XXX is in use by: YYY
because the Nvidia driver gets loaded by default at startup

https://forums.developer.nvidia.com/t/cant-install-new-driver-cannot-unload-module/63639

```bash
$ sudo systemctl disable gdm     # For GNOME Display Manager
$ sudo systemctl disable lightdm # For LightDM
$ sudo systemctl disable sddm    # For SDDM
$ sudo systemctl disable nvidia-persistenced
```

Install the nvidia 555 drivers and the cuda 12.3 toolkit:

```bash
$ wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.0-1_all.deb
$ sudo dpkg -i cuda-keyring_1.0-1_all.deb
$ sudo /usr/sbin/rmmod datadev; sudo /usr/sbin/rmmod nvidia-drm; sudo /usr/sbin/rmmod nvidia-uvm; sudo /usr/sbin/rmmod nvidia-modeset; sudo /usr/sbin/rmmod nvidia; sudo /usr/sbin/rmmod nouveau
$ sudo apt update
$ sudo apt-get purge nvidia-* -y
$ sudo apt autoremove -y
$ sudo apt install cuda-toolkit-12-3 nvidia-kernel-source-555-open libnvidia-compute-555 nvidia-firmware-555-555.42.06 -y
$ sudo reboot
```

Next, Add `iommu=off nouveau.modeset=0 rd.driver.blacklist=nouveau` GRUB_CMDLINE_LINUX:

```bash
$ sudo nano /etc/default/grub
      GRUB_CMDLINE_LINUX="iommu=off nouveau.modeset=0 rd.driver.blacklist=nouveau"
$ sudo update-grub
$ sudo reboot
```

<!--- ######################################################## -->

## How to build and load the nvidia and datadev drviers

After you completed all the "System Configuration" configuration steps above, run the following script to build and load the nvidia and datadev drviers

```bash
$ sudo ./comp_and_load_drivers.sh
```

<!--- ######################################################## -->
