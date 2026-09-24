Build and Load the Driver
=========================

This guide covers building the datadev kernel module for your running kernel,
loading it with insmod, and configuring automatic loading on boot.

.. note::

   This guide applies to the **data_dev** (PCIe/x86) driver. For RCE/ARM
   cross-compilation, see :doc:`cross-compile-rce`.

Prerequisites
-------------

.. code-block:: bash

   sudo apt install linux-headers-$(uname -r) build-essential git

Build the Driver
----------------

.. code-block:: bash

   git clone https://github.com/slaclab/aes-stream-drivers.git
   cd aes-stream-drivers
   make driver
   make app

Output: ``install/$(uname -r)/datadev.ko`` and userspace tools under
``install/bin/``.

Load the Driver
---------------

.. code-block:: bash

   sudo insmod install/$(uname -r)/datadev.ko \
     cfgTxCount=1024 \
     cfgRxCount=1024 \
     cfgSize=131072 \
     cfgMode=1 \
     cfgCont=1

Verify the driver loaded successfully:

.. code-block:: bash

   lsmod | grep datadev
   ls /dev/datadev_*
   cat /proc/datadev_0

Configure Persistent Loading (modprobe.d)
-----------------------------------------

To load the driver automatically at boot with the same parameters, copy the
provided configuration file:

.. code-block:: bash

   sudo cp data_dev/driver/datadev.conf /etc/modprobe.d/datadev.conf

The file sets the following options (edit to match your hardware):

.. code-block:: text

   options datadev cfgTxCount=1024 cfgRxCount=1024 cfgSize=131072 cfgMode=1 cfgCont=1

Then load the driver via modprobe:

.. code-block:: bash

   sudo modprobe datadev

Nodes running the GPU DRP want a different set of parameters, so they have their
own sample. Copy whichever applies, and only one of the two: modprobe reads every
``*.conf`` in that directory and accumulates the ``options`` lines, so two files
disagreeing about a parameter is ambiguous.

.. code-block:: bash

   sudo cp data_dev/driver/datadev-gpu.conf /etc/modprobe.d/datadev.conf

Note that ``/etc/modprobe.d`` is read by ``modprobe``, not by ``insmod``, so a
script that inserts the module directly must pass these parameters on its own
command line.

Install and Reload via DKMS
---------------------------

``dkms-reload.sh`` does the whole sequence in one command, after any pull of the
driver: build the DKMS tarball, register it, build, install, retire any other
datadev package or version, reload the module, and verify that the running module
is also the one ``modprobe`` will pick at the next boot.

.. code-block:: bash

   cd data_dev/driver
   sudo ./dkms-reload.sh          # GPU nodes  (datadev-gpu-dkms)
   sudo ./dkms-reload.sh cpu      # CPU nodes  (datadev-dkms)

It refuses if ``datadev`` is in use, rather than pulling the module out from under
a running application. Both variants build the same ``datadev.ko`` and install it
to the same place, so only one may be installed at a time.

The GPU variant needs the NVIDIA kernel module source registered with DKMS for the
running kernel, since ``PRE_BUILD`` builds against its symbols. If it is missing,
the build fails rather than quietly producing a module without GPU support — which
is indistinguishable from a working one by ``lsmod`` or ``modinfo``, since both
variants are named ``datadev.ko``. Only ``GPUAsync Support`` in
``/proc/datadev_*`` tells them apart. To build without GPU support deliberately,
as CI does, set ``ALLOW_NO_NVIDIA=1``.

Unload the Driver
-----------------

.. code-block:: bash

   sudo rmmod datadev

Parameter Reference
-------------------

For all available module parameters with defaults and valid ranges, see
:doc:`../reference/module-parameters`.
