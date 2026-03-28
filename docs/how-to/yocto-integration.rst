Integrate into a Yocto Project
==============================

This guide covers adding the axistreamdma (or aximemorymap) BitBake recipe
to a Yocto build.

Prerequisites
-------------

- Working Yocto build environment
- An existing layer (or follow Step 1 to create one)
- Cloned aes-stream-drivers repository

Step 1 — Create or Identify Your Layer
---------------------------------------

.. code-block:: bash

   bitbake-layers create-layer $proj_dir/sources/meta-myapplications
   bitbake-layers add-layer $proj_dir/sources/meta-myapplications

.. note::

   Skip this step if you already have a custom layer.

Step 2 — Copy the Recipe
------------------------

.. code-block:: bash

   mkdir -p $proj_dir/sources/meta-myapplications/recipes-kernel
   cp -rfL /path/to/aes-stream-drivers/Yocto/recipes-kernel/axistreamdma \
     $proj_dir/sources/meta-myapplications/recipes-kernel/

.. note::

   The ``-L`` flag dereferences symlinks — required when copying from the
   aes-stream-drivers source tree.

Step 3 — Enable the Module in Your Build
-----------------------------------------

.. code-block:: bash

   echo 'MACHINE_ESSENTIAL_EXTRA_RRECOMMENDS += "axistreamdma"' >> $proj_dir/conf/layer.conf

Optional — auto-load at boot:

.. code-block:: bash

   echo 'KERNEL_MODULE_AUTOLOAD += "axistreamdma"' >> $proj_dir/conf/layer.conf

Step 4 — Set Buffer Parameters
--------------------------------

Add the following to your ``local.conf`` to configure buffer sizes at build
time. These values are passed directly to the kernel module Makefile:

.. code-block:: text

   DMA_TX_BUFF_COUNT = "128"
   DMA_RX_BUFF_COUNT = "128"
   DMA_BUFF_SIZE     = "131072"

Step 5 — Add the Device Tree Node
-----------------------------------

The axistreamdma driver requires a device tree entry. Add the following to
your board's device tree:

.. code-block:: text

   / {
       axi_stream_dma_0@b0000000 {
           compatible = "axi_stream_dma";
           reg = <0x0 0xb0000000 0x0 0x10000>;
           interrupts = <0x0 0x6c 0x4>;
           interrupt-parent = <0x4>;
           slac,acp = <0x0>;
       };
   };

.. note::

   Adjust the ``reg`` base address and ``interrupts`` values to match your
   hardware design.

Step 6 — Build
--------------

.. code-block:: bash

   bitbake core-image-minimal

aximemorymap Variant
--------------------

The ``aximemorymap`` recipe variant
(``Yocto/recipes-kernel/aximemorymap/``) provides memory-mapped register
access. Unlike ``axistreamdma``, it does **not** require a device tree entry
— it is configured via module parameters only. Copy and enable it with the
same steps above, substituting ``aximemorymap`` for ``axistreamdma``.
