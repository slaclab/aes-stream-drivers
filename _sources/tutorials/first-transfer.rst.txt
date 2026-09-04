Your First Data Transfer
========================

By the end of this tutorial you will have built the datadev kernel module,
loaded it, and used ``dmaLoopTest`` to send and receive data through a PCIe
FPGA card. The hardware target is any PCIe x86 system with a SLAC
aes-stream-drivers compatible FPGA card (PCI vendor 0x1a4a, device 0x2030).

.. note::

   **Prerequisite: loopback firmware required.**
   This tutorial requires firmware with internal loopback enabled. Without
   it, ``dmaLoopTest`` will write data but reads will never return. Contact
   your hardware team to confirm your firmware image supports loopback mode
   before proceeding.


Prerequisites
-------------

Before starting, make sure you have:

- A Linux x86_64 host with a free PCIe slot
- An FPGA card with PCI IDs 1a4a:2030 and loopback firmware loaded
- Kernel headers for the running kernel::

      sudo apt install linux-headers-$(uname -r) build-essential

- ``git``, ``make``, and ``gcc`` (all included in ``build-essential``)
- The repository cloned locally::

      git clone https://github.com/slaclab/aes-stream-drivers.git


Step 1 — Detect the Hardware
-----------------------------

Before building anything, confirm the card is visible to the PCIe bus.

.. code-block:: bash

   lspci -d 1a4a:2030

Expected output: one line showing the PCI address and device description,
for example ``01:00.0 Signal processing controller: ...``.

.. note::

   If ``lspci`` shows no output, the card is not recognized by the PCIe bus.
   Check the card's seating in the slot and that the system is fully powered
   before continuing.


Step 2 — Build the Driver and Utilities
-----------------------------------------

Move into the cloned repository and build both the kernel module and the
userspace test applications.

.. code-block:: bash

   cd aes-stream-drivers
   make driver
   make app

Two important outputs are created:

- ``install/$(uname -r)/datadev.ko`` — the kernel module for the running kernel
- ``install/bin/dmaLoopTest`` — the loopback test application


Step 3 — Load the Driver
--------------------------

Load the kernel module with parameters suited for a loopback test.

.. code-block:: bash

   sudo insmod install/$(uname -r)/datadev.ko \
     cfgSize=131072 \
     cfgRxCount=1024 \
     cfgTxCount=1024 \
     cfgMode=1 \
     cfgCont=1 \
     cfgIrqDis=0

What each parameter means:

- ``cfgSize=131072`` — DMA buffer size in bytes (128 KiB per buffer)
- ``cfgRxCount=1024`` — number of receive buffers in the pool
- ``cfgTxCount=1024`` — number of transmit buffers in the pool
- ``cfgMode=1`` — coherent DMA mode (CPU and device see the same memory)
- ``cfgCont=1`` — continuous receive mode; the driver re-queues buffers automatically
- ``cfgIrqDis=0`` — use interrupts (not polling)

For the full parameter reference, see :doc:`../reference/module-parameters`.

Verify the module loaded and the device node appeared:

.. code-block:: bash

   lsmod | grep datadev
   ls /dev/datadev_0

Expected: ``datadev`` appears in the ``lsmod`` output and ``/dev/datadev_0``
exists.


Step 4 — Inspect the /proc Interface
---------------------------------------

The driver exposes diagnostic information through ``/proc``. The
``/proc/datadev_0`` file reports firmware registers, buffer pool status,
and driver version information.

.. code-block:: bash

   cat /proc/datadev_0

You will see output structured like this:

.. code-block:: text

   PCIe[BUS:NUM:SLOT.FUNC] : <pci address>

   ---------- DMA Firmware General ----------
                       IRQ : <irq number>
             Int Req Count : <count>
            Continue Count : <count>
             Address Count : <count>
       Hw Write Buff Count : <count>
        Hw Read Buff Count : <count>
              Cache Config : 0x<hex>
               Desc 128 En : <0 or 1>
               Enable Ver  : 0x<hex>
         Driver Load Count : <count>

   -------- DMA Kernel Driver General --------
    DMA Driver's Git Version : <hash>
    DMA Driver's API Version : 0x<hex>
            GPUAsync Support : Enabled/Disabled

   ---- Read Buffers (Firmware->Software) ----
            Buffer Count : 1024
             Buffer Size : 131072
             Buffer Mode : 1
         Buffers In User : <count>
           Buffers In Hw : <count>
     Buffers In Pre-Hw Q : <count>
     Buffers In Rx Queue : <count>
          Tot Buffer Use : <count>

   ---- Write Buffers (Software->Firmware) ---
            Buffer Count : 1024
             Buffer Size : 131072
             Buffer Mode : 1
         Buffers In User : <count>
           Buffers In Hw : <count>
     Buffers In Pre-Hw Q : <count>
     Buffers In Sw Queue : <count>
          Tot Buffer Use : <count>

Two things to verify before moving on:

- **Buffer Count** under both Read and Write sections should be ``1024``,
  matching the ``cfgRxCount=1024`` and ``cfgTxCount=1024`` parameters.
- **Buffer Mode** should be ``1``, matching ``cfgMode=1`` (coherent DMA).

If either value is different, the module was loaded with different parameters.
Unload it with ``sudo rmmod datadev`` and repeat Step 3.


Step 5 — Run the Loopback Test
--------------------------------

Now send data through the firmware loopback and receive it back.

.. code-block:: bash

   sudo ./install/bin/dmaLoopTest \
     --path /dev/datadev_0 \
     --dest 0 \
     --size 10000 \
     --fuser 0x2 \
     --luser 0x0

What each flag means:

- ``--path /dev/datadev_0`` — the device node to open
- ``--dest 0`` — route transfers to AXI stream channel 0
- ``--size 10000`` — transfer size in bytes per frame
- ``--fuser 0x2`` — AXI stream first-user bits (application-defined framing marker)
- ``--luser 0x0`` — AXI stream last-user bits

For AXI stream framing details, see :doc:`../explanation/axi-stream-protocol`.

Expected output: ``dmaLoopTest`` prints write and read throughput statistics.
If it exits with status 0 the loopback is working and data is flowing
correctly through the FPGA and back.

.. note::

   If ``dmaLoopTest`` hangs after printing write statistics, the firmware does
   not have loopback enabled. Press ``Ctrl-C``, verify your firmware image
   supports loopback mode, and retry.


Step 6 — Unload the Driver
----------------------------

When you are done, unload the kernel module cleanly.

.. code-block:: bash

   sudo rmmod datadev

Verify the module is gone:

.. code-block:: bash

   lsmod | grep datadev

The command should produce no output.


Next Steps
----------

Now that you have data flowing end to end, you can:

- Build and load the driver persistently across reboots:
  :doc:`../how-to/build-and-load`
- Understand the DMA buffer modes and when to use each:
  :doc:`../explanation/dma-modes`
- Route data to multiple channels simultaneously:
  :doc:`../how-to/multi-channel`
- Set up GPUDirect RDMA for zero-copy GPU transfers:
  :doc:`gpudirect-first`
