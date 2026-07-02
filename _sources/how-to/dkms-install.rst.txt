Install via DKMS
================

This guide covers installing the datadev driver using DKMS (Dynamic Kernel Module Support)
so it rebuilds automatically when the kernel is upgraded.

DKMS allows kernel modules to be installed once and automatically rebuilt for each new
kernel version, eliminating the need to manually recompile drivers after kernel updates.

Prerequisites
-------------

Ubuntu / Debian
~~~~~~~~~~~~~~~

DKMS is available in the standard repositories:

.. code-block:: bash

   sudo apt-get update
   sudo apt-get install dkms build-essential linux-headers-$(uname -r)

Rocky Linux 9 / RHEL 9
~~~~~~~~~~~~~~~~~~~~~~

DKMS requires the EPEL (Extra Packages for Enterprise Linux) repository:

.. code-block:: bash

   # Enable EPEL
   sudo dnf install epel-release

   # Install DKMS and build dependencies
   sudo dnf install dkms kernel-devel kernel-headers gcc gcc-c++ make elfutils-libelf-devel

CentOS 7 / RHEL 7
~~~~~~~~~~~~~~~~~

DKMS requires the EPEL repository:

.. code-block:: bash

   # Enable EPEL
   sudo yum install epel-release

   # Install DKMS and build dependencies
   sudo yum install dkms kernel-devel kernel-headers gcc gcc-c++ make elfutils-libelf-devel

RHEL with Subscription
~~~~~~~~~~~~~~~~~~~~~~

If using RHEL with an active subscription, enable CodeReady Builder first:

.. code-block:: bash

   # RHEL 9
   sudo subscription-manager repos --enable codeready-builder-for-rhel-9-$(arch)-rpms
   sudo dnf install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
   sudo dnf install dkms kernel-devel kernel-headers gcc gcc-c++ make elfutils-libelf-devel

   # RHEL 7
   sudo subscription-manager repos --enable rhel-7-server-optional-rpms
   sudo yum install https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
   sudo yum install dkms kernel-devel kernel-headers gcc gcc-c++ make elfutils-libelf-devel

Installation Methods
--------------------

There are two installation methods:

1. **Pre-built Tarball** (Recommended) — Download and install a release tarball
2. **Manual Source Copy** — Copy sources to DKMS tree and configure manually

Method 1: Install from Release Tarball (Recommended)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This method works on **all distributions** (Ubuntu, Debian, Rocky Linux, CentOS, RHEL).

Download the appropriate tarball from the `Releases page <https://github.com/slaclab/aes-stream-drivers/releases>`_:

- **CPU version** (no GPU support): ``datadev-dkms-7.2.2.tar.gz``
- **GPU version** (with GPUDirect RDMA): ``datadev-gpu-dkms-7.2.2.tar.gz``

.. note::

   Release tags do **not** carry a ``v`` prefix (e.g. ``7.2.2``, not ``v7.2.2``).
   Use the bare version string in both the download URL and the ``dkms`` commands.

CPU Version
^^^^^^^^^^^

.. code-block:: bash

   # Download the CPU tarball
   VERSION=7.2.2  # Replace with latest release
   wget https://github.com/slaclab/aes-stream-drivers/releases/download/${VERSION}/datadev-dkms-${VERSION}.tar.gz

   # Install with DKMS
   sudo dkms ldtarball datadev-dkms-${VERSION}.tar.gz
   sudo dkms install datadev-dkms/${VERSION}

   # Load the module
   sudo modprobe datadev

GPU Version
^^^^^^^^^^^

.. code-block:: bash

   # Download the GPU tarball
   VERSION=7.2.2  # Replace with latest release
   wget https://github.com/slaclab/aes-stream-drivers/releases/download/${VERSION}/datadev-gpu-dkms-${VERSION}.tar.gz

   # Install with DKMS
   sudo dkms ldtarball datadev-gpu-dkms-${VERSION}.tar.gz
   sudo dkms install datadev-gpu-dkms/${VERSION}

   # Load the module
   sudo modprobe datadev

.. note::

   The GPU variant requires NVIDIA Open Kernel Modules to be installed first.
   See :doc:`gpudirect-setup` for GPU prerequisites.

Method 2: Manual Source Installation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use this method if you're building from a git checkout rather than a release.

Step 1 — Copy Sources to the DKMS Tree
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: bash

   VERSION=7.2.2   # Replace with your version
   sudo mkdir -p /usr/src/datadev-dkms-${VERSION}
   sudo cp -r data_dev/driver/src/* /usr/src/datadev-dkms-${VERSION}/
   sudo cp data_dev/driver/Makefile /usr/src/datadev-dkms-${VERSION}/
   sudo cp data_dev/driver/dkms.conf /usr/src/datadev-dkms-${VERSION}/
   sudo cp data_dev/driver/datadev.conf /usr/src/datadev-dkms-${VERSION}/
   sudo cp data_dev/driver/rc.local.in /usr/src/datadev-dkms-${VERSION}/

Step 2 — Set the Package Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. warning::

   The ``dkms.conf`` file in the repository has ``PACKAGE_VERSION`` commented
   out. You **must** set it before running ``dkms add``, or the command will fail.

.. code-block:: bash

   echo "PACKAGE_VERSION=${VERSION}" | sudo tee -a /usr/src/datadev-dkms-${VERSION}/dkms.conf

After editing, the key fields in ``dkms.conf`` should read:

.. code-block:: text

   MAKE="make"
   CLEAN="make clean"
   BUILT_MODULE_NAME=datadev
   BUILT_MODULE_LOCATION=.
   DEST_MODULE_LOCATION="/kernel/modules/misc"
   PACKAGE_NAME=datadev-dkms
   PACKAGE_VERSION=7.2.2
   REMAKE_INITRD=no
   AUTOINSTALL="yes"

Step 3 — Add, Build, and Install
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: bash

   sudo dkms add datadev-dkms/${VERSION}
   sudo dkms build datadev-dkms/${VERSION}
   sudo dkms install datadev-dkms/${VERSION}
   sudo modprobe datadev

Verification
------------

Check Installation Status
~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: bash

   dkms status

Expected output:

.. code-block:: text

   datadev-dkms/7.2.2, 5.15.0-91-generic, x86_64: installed

Or for GPU variant:

.. code-block:: text

   datadev-gpu-dkms/7.2.2, 5.15.0-91-generic, x86_64: installed

Verify Module Information
~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: bash

   modinfo datadev

Expected output should show version, description, and parameters:

.. code-block:: text

   filename:       /lib/modules/5.15.0-91-generic/updates/dkms/datadev.ko
   license:        GPL
   description:    AXI Stream Driver
   srcversion:     ABC123DEF456
   depends:
   ...

Load the Module
~~~~~~~~~~~~~~~

.. code-block:: bash

   sudo modprobe datadev

   # Verify device node created
   ls -l /dev/datadev_0

Auto-load on Boot
~~~~~~~~~~~~~~~~~

To load the module automatically at boot:

Ubuntu / Debian
^^^^^^^^^^^^^^^

.. code-block:: bash

   echo "datadev" | sudo tee /etc/modules-load.d/datadev.conf

Rocky Linux / CentOS / RHEL
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: bash

   echo "datadev" | sudo tee /etc/modules-load.d/datadev.conf

Uninstallation
--------------

Remove DKMS Module
~~~~~~~~~~~~~~~~~~

.. code-block:: bash

   VERSION=7.2.2  # Your installed version

   # Unload the module
   sudo modprobe -r datadev

   # Uninstall from DKMS (use datadev-gpu-dkms for the GPU variant)
   sudo dkms uninstall datadev-dkms/${VERSION}
   sudo dkms remove datadev-dkms/${VERSION} --all

   # Remove auto-load configuration
   sudo rm -f /etc/modules-load.d/datadev.conf

Platform-Specific Notes
-----------------------

Ubuntu 22.04 / 24.04
~~~~~~~~~~~~~~~~~~~~

Works out of the box with the standard ``linux-headers-$(uname -r)`` package.

Rocky Linux 9
~~~~~~~~~~~~~

- Kernel 5.14+ (frankenstein kernel)
- EPEL repository required for DKMS
- Use ``dnf`` package manager

CentOS 7
~~~~~~~~

- Kernel 3.10
- EPEL repository required for DKMS
- CentOS 7 reached EOL June 2024, use vault mirrors
- Use ``yum`` package manager

Debian Testing / Experimental
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- Latest upstream kernel
- DKMS available in standard repositories
- May require ``linux-headers-amd64`` metapackage

Troubleshooting
---------------

DKMS Build Failures
~~~~~~~~~~~~~~~~~~~

If ``dkms install`` fails with compilation errors:

.. code-block:: bash

   # Check kernel headers are installed
   ls /lib/modules/$(uname -r)/build

   # View detailed build log
   sudo dkms status -m datadev-dkms
   cat /var/lib/dkms/datadev-dkms/${VERSION}/build/make.log

Missing Kernel Headers
~~~~~~~~~~~~~~~~~~~~~~

**Ubuntu/Debian:**

.. code-block:: bash

   sudo apt-get install linux-headers-$(uname -r)

**Rocky Linux 9:**

.. code-block:: bash

   sudo dnf install kernel-devel-$(uname -r) kernel-headers

**CentOS 7:**

.. code-block:: bash

   sudo yum install kernel-devel-$(uname -r) kernel-headers

EPEL Not Available
~~~~~~~~~~~~~~~~~~

If EPEL installation fails on RHEL:

.. code-block:: bash

   # Verify subscription is active
   sudo subscription-manager status

   # Enable required repositories
   sudo subscription-manager repos --enable codeready-builder-for-rhel-9-$(arch)-rpms

Module Load Failures
~~~~~~~~~~~~~~~~~~~~

If ``modprobe datadev`` fails:

.. code-block:: bash

   # Check for errors
   sudo dmesg | tail -20

   # Verify module file exists
   find /lib/modules/$(uname -r) -name datadev.ko

Kernel Version Mismatch
~~~~~~~~~~~~~~~~~~~~~~~

DKMS must rebuild modules for each kernel. If you boot a different kernel:

.. code-block:: bash

   # Check if module is built for current kernel
   dkms status

   # Rebuild for current kernel if missing
   sudo dkms autoinstall

See Also
--------

- :doc:`gpudirect-setup` — GPU variant prerequisites and NVIDIA driver setup
- :doc:`build-and-load` — Manual build without DKMS
- :doc:`../reference/module-parameters` — Configuration options
