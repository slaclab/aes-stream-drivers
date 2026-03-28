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

Unload the Driver
-----------------

.. code-block:: bash

   sudo rmmod datadev

Parameter Reference
-------------------

For all available module parameters with defaults and valid ranges, see
:doc:`../reference/module-parameters`.
