Install via DKMS
================

This guide covers installing the datadev driver using DKMS so it rebuilds
automatically when the kernel is upgraded.

Prerequisites
-------------

.. code-block:: bash

   sudo apt install dkms build-essential linux-headers-$(uname -r)

Step 1 — Copy Sources to the DKMS Tree
----------------------------------------

.. code-block:: bash

   VERSION=v2024.09   # replace with your release tag
   sudo mkdir -p /usr/src/datadev-${VERSION}
   sudo cp -r data_dev/driver/src/* /usr/src/datadev-${VERSION}/
   sudo cp data_dev/driver/dkms.conf /usr/src/datadev-${VERSION}/

Step 2 — Set the Package Version
----------------------------------

.. warning::

   The ``dkms.conf`` file in the repository has ``PACKAGE_VERSION`` commented
   out. You **must** uncomment it and set it to your version string before
   running ``dkms add``, or the add command will fail.

.. code-block:: bash

   sudo nano /usr/src/datadev-${VERSION}/dkms.conf
   # Uncomment and set:
   # PACKAGE_VERSION="${VERSION}"

After editing, the key fields in ``dkms.conf`` should read:

.. code-block:: text

   MAKE="make"
   CLEAN="make clean"
   BUILT_MODULE_NAME=datadev
   BUILT_MODULE_LOCATION=.
   DEST_MODULE_LOCATION="/kernel/modules/misc"
   PACKAGE_NAME=datadev-dkms
   PACKAGE_VERSION=v2024.09
   REMAKE_INITRD=no
   AUTOINSTALL="yes"

Step 3 — Add, Build, and Install
----------------------------------

.. code-block:: bash

   sudo dkms add datadev/${VERSION}
   sudo dkms build datadev/${VERSION}
   sudo dkms install datadev/${VERSION}

Verify
------

.. code-block:: bash

   dkms status

Expected output: ``datadev, ${VERSION}, <kernel>, x86_64: installed``

GPU Variant
-----------

For GPUDirect support, use ``dkms-gpu.conf`` instead. This variant is named
``datadev-gpu-dkms`` and runs ``build-nvidia.sh`` (via ``PRE_BUILD``) before
the kernel module build to compile the NVIDIA open kernel modules.

.. code-block:: bash

   sudo cp data_dev/driver/dkms-gpu.conf /usr/src/datadev-${VERSION}/dkms.conf
   # Edit PACKAGE_VERSION as above, then:
   sudo dkms add datadev/${VERSION}
   sudo dkms build datadev/${VERSION}
   sudo dkms install datadev/${VERSION}

.. note::

   The GPU variant requires NVIDIA Open Kernel Modules to be installed first.
   See :doc:`gpudirect-setup`.
