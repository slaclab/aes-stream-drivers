Device Nodes
============

The ``datadev`` driver creates one character device and one ``/proc`` entry
per detected PCIe card.

.. list-table:: Quick Reference
   :header-rows: 1
   :widths: 30 15 55

   * - Node
     - Permissions
     - Description
   * - ``/dev/datadev_N``
     - ``0666``
     - Primary read/write interface. Open with ``O_RDWR``.
   * - ``/proc/datadev_N``
     - read-only
     - Diagnostic state dump. Read with ``cat /proc/datadev_0``.


/dev/ — Character Devices
--------------------------

The driver registers a character device for each hardware card using a
dynamically allocated major:minor number. The device name is controlled by
the ``cfgDevName`` module parameter.

**Default naming** (``cfgDevName=0``): sequential probe-order index.

.. code-block:: console

   /dev/datadev_0    # first card detected
   /dev/datadev_1    # second card detected

**PCI bus-number naming** (``cfgDevName`` nonzero): hex PCI bus number.

.. code-block:: console

   /dev/datadev_1a   # card on PCI bus 0x1a
   /dev/datadev_1b   # card on PCI bus 0x1b

Use PCI bus-number naming when probe order is non-deterministic (e.g., in
systems where PCIe slot enumeration order varies across reboots).

**Permissions:** ``0666`` — all users can open the device without ``sudo``
after the driver loads. This is set unconditionally in ``Dma_DevNode()``.

**Maximum devices:** up to 32 cards per system (``MAX_DMA_DEVICES = 32``).

**Opening the device:**

.. code-block:: c

   int fd = open("/dev/datadev_0", O_RDWR);
   if (fd < 0) { perror("open"); return 1; }


/proc/ — Diagnostic Interface
------------------------------

Each device also creates a ``/proc/`` entry with the same name as the
``/dev/`` node:

.. code-block:: console

   /proc/datadev_0   # diagnostic dump for the first card

The ``/proc`` interface is read-only. It prints driver state including
DMA buffer pool statistics, AXIS stream status, firmware version (from
``AxiVersion``), and GPU state if GPU support is compiled in.

.. code-block:: console

   $ cat /proc/datadev_0
   (driver state output — useful for verifying the driver loaded correctly
    and for diagnosing buffer exhaustion or DMA errors)

The ``/proc`` entry is removed when the driver is unloaded or the card
is removed.


Multi-Device Systems
--------------------

When multiple cards are present, each card gets its own ``/dev/`` and
``/proc/`` entry. Applications address a specific card by opening its
device node:

.. code-block:: c

   int fd0 = open("/dev/datadev_0", O_RDWR);  /* first card  */
   int fd1 = open("/dev/datadev_1", O_RDWR);  /* second card */

DMA buffer pools and channel masks are per-device — operations on ``fd0``
do not affect ``fd1``.
