# GPU Enabled Driver

To build this driver you need to have the NVIDA Open GPU Kernel Modules installed. This driver will not compile against the CUDA toolkit drivers.

https://docs.nvidia.com/cuda/cuda-installation-guide-linux/index.html

See section 5.

<!--- ######################################################## -->

# System Configurations

Disable the Xserver and nvidia-persistenced to prevent rmmod due to Module XXX is in use by: YYY
because the Nvidia driver gets loaded by default at startup 

https://forums.developer.nvidia.com/t/cant-install-new-driver-cannot-unload-module/63639

```bash
$ sudo systemctl disable gdm     # For GNOME Display Manager
$ sudo systemctl disable lightdm # For LightDM
$ sudo systemctl disable sddm    # For SDDM
$ sudo systemctl disable nvidia-persistenced
```

Add the nvida cuda package for nvidia-545.23.08 and install the cuda toolkit:

```bash
$ wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.0-1_all.deb
$ sudo dpkg -i cuda-keyring_1.0-1_all.deb
$ sudo apt update
$ sudo apt install nvidia-kernel-source-545 # Tested with nvidia-545.23.08
$ sudo apt install nvidia-cuda-toolkit
```

Add `iommu=off nouveau.modeset=0 rd.driver.blacklist=nouveau` GRUB_CMDLINE_LINUX:

```bash
$ sudo nano /etc/default/grub
      GRUB_CMDLINE_LINUX="iommu=off nouveau.modeset=0 rd.driver.blacklist=nouveau"
$ sudo update-grub
$ sudo reboot
```

<!--- ######################################################## -->

# How to build and load the nvidia and datagpu drviers

After you completed all the "System Configuration" configuration steps above, run the following script to build and load the nvidia and datagpu drviers

```bash
$ sudo ./comp_and_load_drivers.sh
```

<!--- ######################################################## -->
