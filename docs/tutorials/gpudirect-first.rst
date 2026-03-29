Your First GPUDirect Transfer
==============================

By the end of this tutorial you will have installed the NVIDIA Open Kernel
Modules, configured GRUB, used ``comp_and_load_drivers.sh`` to build and
load all required drivers, and run ``rdmaTest`` to confirm GPU-to-FPGA DMA
works. This requires an NVIDIA GPU and a PCIe FPGA card (PCI IDs 1a4a:2030)
installed in the same PCIe bus on a Linux x86_64 host.

.. warning::

   GPUDirect RDMA requires NVIDIA Open Kernel Modules
   (``nvidia-kernel-source-590-open``), **not** the standard CUDA toolkit
   kernel driver. Using the wrong driver type will cause
   ``comp_and_load_drivers.sh`` to fail with
   ``Could not find NVIDIA open drivers in /usr/src``. See
   :doc:`../how-to/gpudirect-setup` for the competent-user reference version
   of these steps.


Prerequisites
-------------

Before starting, make sure you have:

- A Linux x86_64 host (Ubuntu 22.04 — these steps use Ubuntu ``apt`` commands)
- An NVIDIA GPU installed in a PCIe slot adjacent to the FPGA card
- An FPGA card with PCI IDs 1a4a:2030
- Root or ``sudo`` access
- The repository cloned locally::

      git clone https://github.com/slaclab/aes-stream-drivers.git


Step 1 — Disable the Display Manager
---------------------------------------

GPUDirect requires NVIDIA kernel modules to have exclusive access to the GPU.
Display managers load the NVIDIA driver at startup and prevent it from being
unloaded and rebuilt. Disable them now.

.. code-block:: bash

   sudo systemctl disable gdm
   sudo systemctl disable lightdm
   sudo systemctl disable sddm
   sudo systemctl disable nvidia-persistenced

.. note::

   Only disable the services that are installed on your system. It is safe
   to run all four commands — they will fail gracefully for services that
   do not exist.

Reboot after disabling the display manager before proceeding.


Step 2 — Install NVIDIA Open Kernel Modules
---------------------------------------------

.. warning::

   Install ``nvidia-kernel-source-590-open`` (the open kernel module), not
   the standard ``cuda-drivers`` package. The open module provides the
   ``nv-p2p.h`` peer-to-peer interface required for GPUDirect. The
   ``comp_and_load_drivers.sh`` script searches for this header to locate
   the driver sources.

Add the NVIDIA CUDA package repository and install the required packages.

.. code-block:: bash

   wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.0-1_all.deb
   sudo dpkg -i cuda-keyring_1.0-1_all.deb
   sudo apt update
   sudo apt-get purge nvidia-* -y
   sudo apt autoremove -y
   sudo apt install cuda-toolkit-13-1 nvidia-kernel-source-590-open libnvidia-compute-590 cuda-compat-13-1 nvidia-firmware

Then reboot:

.. code-block:: bash

   sudo reboot

.. note::

   Review the package install list carefully. NVIDIA packages may pull in a
   low-latency kernel image. Make sure the resulting kernel is the one you
   intend to run.


Step 3 — Configure GRUB
--------------------------

Disable IOMMU and the ``nouveau`` open-source NVIDIA driver, which conflicts
with the NVIDIA proprietary modules.

Open the GRUB configuration file:

.. code-block:: bash

   sudo nano /etc/default/grub

Find the ``GRUB_CMDLINE_LINUX`` line and set it to:

.. code-block:: text

   GRUB_CMDLINE_LINUX="iommu=off nouveau.modeset=0 rd.driver.blacklist=nouveau"

Save the file, then update GRUB and reboot:

.. code-block:: bash

   sudo update-grub
   sudo reboot


Step 4 — Build and Load All Drivers
--------------------------------------

The ``comp_and_load_drivers.sh`` script handles the full build and load
sequence in one step. It locates the NVIDIA open driver sources, rebuilds
and loads them with the correct parameters, then builds and loads ``datadev``.

From the root of the cloned repository:

.. code-block:: bash

   cd aes-stream-drivers
   sudo ./data_dev/driver/comp_and_load_drivers.sh

What the script does:

- Searches ``/usr/src`` for ``nv-p2p.h`` to locate the NVIDIA open driver sources
- Unloads any existing ``nvidia`` and ``datadev`` modules
- Rebuilds the NVIDIA drivers using the same compiler the kernel was built with
- Loads ``nvidia.ko`` with ``NVreg_OpenRmEnableUnsupportedGpus=1 NVreg_EnableStreamMemOPs=1``
- Builds and insmod-s ``datadev.ko``

When ``comp_and_load_drivers.sh`` completes successfully, verify both modules
are loaded:

.. code-block:: bash

   lsmod | grep datadev
   lsmod | grep nvidia

Both should appear in the output.


Step 5 — Verify GPUDirect Support
------------------------------------

Before running the transfer test, confirm that the loaded firmware supports
GPUDirect. The ``gpuIsGpuAsyncSupported`` function in ``GpuAsync.h`` checks
for the GpuAsyncCore IP block in the firmware.

.. code-block:: c

   #include <GpuAsync.h>
   #include <fcntl.h>
   #include <stdio.h>

   int fd = open("/dev/datadev_0", O_RDWR);
   if (gpuIsGpuAsyncSupported(fd)) {
       printf("GPUDirect supported\n");
   } else {
       printf("Firmware does not have GpuAsyncCore — GPUDirect not available\n");
   }

.. note::

   If ``gpuIsGpuAsyncSupported`` returns 0, your firmware does not include
   the GpuAsyncCore IP block. GPUDirect transfers will not work until the
   FPGA is reprogrammed with firmware that includes GpuAsyncCore. Contact
   your hardware team.


Step 6 — Run rdmaTest
-----------------------

With both drivers loaded and GPUDirect support confirmed, run the test:

.. code-block:: bash

   sudo ./data_dev/app/bin/rdmaTest

Expected output: ``rdmaTest`` prints GPU-to-FPGA transfer statistics and
exits with status 0. If it exits cleanly the DMA path from the GPU through
the PCIe bus to the FPGA card is working correctly.


Next Steps
----------

Now that GPUDirect is working, you can:

- Set up GPUDirect without the tutorial hand-holding:
  :doc:`../how-to/gpudirect-setup`
- Understand DMA buffer modes and zero-copy transfers:
  :doc:`../explanation/dma-modes`
- Route data to specific channels for multi-stream applications:
  :doc:`../how-to/multi-channel`
