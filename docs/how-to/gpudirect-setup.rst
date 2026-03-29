Set Up GPUDirect RDMA
====================

This guide covers the system configuration required to enable GPUDirect RDMA
between an NVIDIA GPU and an FPGA card using the datadev driver.

.. warning::

   You must install **NVIDIA Open Kernel Modules**
   (``nvidia-kernel-source-590-open``), not the standard CUDA toolkit kernel
   driver. The open module provides the ``nv-p2p.h`` peer-to-peer interface.
   If ``comp_and_load_drivers.sh`` outputs
   ``Could not find NVIDIA open drivers in /usr/src``, the wrong driver type
   is installed.

.. note::

   For a tutorial walkthrough of these steps with more explanation, see
   :doc:`../tutorials/gpudirect-first`.

Prerequisites
-------------

- Ubuntu 22.04 x86_64
- NVIDIA GPU and FPGA card on the same PCIe bus
- sudo access

Step 1 — Disable Display Manager Services
------------------------------------------

Disable display managers and the NVIDIA persistence daemon to prevent
``Module XXX is in use`` errors during driver replacement:

.. code-block:: bash

   sudo systemctl disable gdm
   sudo systemctl disable lightdm
   sudo systemctl disable sddm
   sudo systemctl disable nvidia-persistenced

Step 2 — Remove Existing NVIDIA Drivers
-----------------------------------------

.. code-block:: bash

   sudo apt-get purge nvidia-* -y
   sudo apt autoremove -y

Step 3 — Install NVIDIA Open Kernel Modules
--------------------------------------------

.. code-block:: bash

   wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.0-1_all.deb
   sudo dpkg -i cuda-keyring_1.0-1_all.deb
   sudo apt update
   sudo apt install cuda-toolkit-13-1 nvidia-kernel-source-590-open \
     libnvidia-compute-590 cuda-compat-13-1 nvidia-firmware
   sudo reboot

.. warning::

   Review the package install list. NVIDIA packages may pull in a low-latency
   kernel image.

Step 4 — Configure GRUB
------------------------

.. code-block:: bash

   sudo nano /etc/default/grub
   # Set GRUB_CMDLINE_LINUX to:
   # GRUB_CMDLINE_LINUX="iommu=off nouveau.modeset=0 rd.driver.blacklist=nouveau"
   sudo update-grub
   sudo reboot

Step 5 — Build and Load Both Drivers
--------------------------------------

.. code-block:: bash

   cd aes-stream-drivers
   sudo ./data_dev/driver/comp_and_load_drivers.sh

What the script does:

- Locates NVIDIA open driver sources by searching ``/usr/src`` for ``nv-p2p.h``
- Unloads existing nvidia and datadev modules
- Rebuilds and loads NVIDIA drivers with ``NVreg_OpenRmEnableUnsupportedGpus=1``
  and ``NVreg_EnableStreamMemOPs=1``
- Builds and loads ``datadev.ko``

Verify
------

.. code-block:: bash

   lsmod | grep datadev
   lsmod | grep nvidia

Then in C, verify GPUDirect firmware support:

.. code-block:: c

   int fd = open("/dev/datadev_0", O_RDWR);
   if (gpuIsGpuAsyncSupported(fd)) {
       printf("GPUDirect supported\n");
   }

For background on DMA buffer modes used by GPUDirect, see
:doc:`../explanation/dma-modes`.
