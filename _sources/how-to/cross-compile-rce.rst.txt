Cross-Compile for RCE (ARM/Zynq)
=================================

This guide covers building the rce_stream and rce_memmap drivers for ARM/Zynq
targets using the Xilinx toolchain.

.. note::

   This guide applies to the **RCE** (ARM/Zynq) drivers. For PCIe/x86 native
   build, see :doc:`build-and-load`.

Prerequisites
-------------

- Xilinx ARM cross-compiler (e.g., ``arm-xilinx-linux-gnueabi-``)
- Xilinx Linux kernel source (``linux-xlnx`` branch matching your BSP version)
- ``make``, ``git``

Build at SLAC (Shortcut)
------------------------

At SLAC, the Xilinx kernel and toolchain are pre-installed. Use the top-level
make target:

.. code-block:: bash

   make rce

This builds rce_stream, rce_memmap, and rce_hp drivers against the kernel at
``/sdf/group/faders/tools/xilinx/rce_linux_kernel/``.

Build Outside SLAC
------------------

ARM/Zynq Target
~~~~~~~~~~~~~~~

.. code-block:: bash

   make -C rce_stream/driver KDIR=/path/to/xilinx/linux-xlnx-v2016.4
   make -C rce_memmap/driver KDIR=/path/to/xilinx/linux-xlnx-v2016.4

x86 Buildroot Target
~~~~~~~~~~~~~~~~~~~~

.. code-block:: bash

   make \
     ARCH=x86_64 \
     CROSS_COMPILE=/path/to/buildroot/host/linux-x86_64/x86_64/usr/bin/x86_64-buildroot-linux-gnu- \
     KERNELDIR=/path/to/buildroot/output/build/linux-4.14.139

Substitute the actual paths to your Xilinx toolchain and kernel source. The
SLAC paths shown in the Makefile are site-specific.

Output Location
---------------

After a successful build, the compiled modules are placed in:

.. code-block:: text

   install/<kernel_version>.arm/rce_stream.ko
   install/<kernel_version>.arm/rce_memmap.ko

The ``<kernel_version>`` string is determined from the Xilinx kernel source
directory.
