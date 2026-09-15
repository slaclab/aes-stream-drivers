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

Disable the Xserver and nvidia-persistenced to prevent rmmod failure due to `Module XXX is in use by: YYY` error.
because the Nvidia driver gets loaded by default at startup

https://forums.developer.nvidia.com/t/cant-install-new-driver-cannot-unload-module/63639

```bash
$ sudo systemctl disable gdm     # For GNOME Display Manager
$ sudo systemctl disable lightdm # For LightDM
$ sudo systemctl disable sddm    # For SDDM
$ sudo systemctl disable nvidia-persistenced
```

Install the nvidia 590 drivers and the cuda 13.1 toolkit:

```bash
$ wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.0-1_all.deb
$ sudo dpkg -i cuda-keyring_1.0-1_all.deb
$ sudo /usr/sbin/rmmod datadev; sudo /usr/sbin/rmmod nvidia-drm; sudo /usr/sbin/rmmod nvidia-uvm; sudo /usr/sbin/rmmod nvidia-modeset; sudo /usr/sbin/rmmod nvidia; sudo /usr/sbin/rmmod nouveau
$ sudo apt update
$ sudo apt-get purge nvidia-* -y
$ sudo apt autoremove -y
$ sudo apt install cuda-toolkit-13-1 nvidia-kernel-source-590-open libnvidia-compute-590 cuda-compat-13-1 nvidia-firmware
$ sudo reboot
```

WARNING: Make sure to review the package install list, as the NVIDIA drivers may pull in a low-latency kernel image.

Next, Add `iommu=off nouveau.modeset=0 rd.driver.blacklist=nouveau` GRUB_CMDLINE_LINUX:

```bash
$ sudo nano /etc/default/grub
      GRUB_CMDLINE_LINUX="iommu=off nouveau.modeset=0 rd.driver.blacklist=nouveau"
$ sudo update-grub
$ sudo reboot
```

<!--- ######################################################## -->

## How to build and load the nvidia and datadev drivers

After completing all the "System Configuration" steps above, run the following script to build and load the nvidia and datadev drivers

```bash
$ sudo ./comp_and_load_drivers.sh
```

That builds and `insmod`s the module in place, so the driver is not installed where
`modprobe` can find it and the node will not come up with it after a reboot.  It also
passes the module parameters on its own command line, so `/etc/modprobe.d` does not
affect it.

<!--- ######################################################## -->

## How to install and reload via DKMS

`dkms-reload.sh` does the whole sequence in one command, after any pull of the driver:
build the DKMS tarball, register it, build, install, retire any other datadev package
or version, reload the module, and verify that the running module is also the one
`modprobe` will pick at the next boot.

```bash
$ sudo ./dkms-reload.sh          # GPU nodes  (datadev-gpu-dkms)
$ sudo ./dkms-reload.sh cpu      # CPU nodes  (datadev-dkms)
```

It refuses if `datadev` is in use rather than pulling the module out from under a
running application.  Both variants build the same `datadev.ko` and install it to the
same place, so only one may be installed at a time.

The GPU variant needs the NVIDIA kernel module source registered with DKMS for the
running kernel.  If it is missing the build fails, rather than quietly producing a
module without GPU support: both variants are named `datadev.ko`, so `lsmod` and
`modinfo` cannot tell them apart and only `GPUAsync Support` in `/proc/datadev_*`
does.  Set `ALLOW_NO_NVIDIA=1` to build without GPU support deliberately, as CI does.

Module parameters come from `/etc/modprobe.d/datadev.conf`; copy `datadev.conf` for a
CPU node or `datadev-gpu.conf` for one running the GPU DRP, and only one of the two.

<!--- ######################################################## -->
