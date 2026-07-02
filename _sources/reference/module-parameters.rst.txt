Module Parameters — datadev
============================

The ``datadev`` kernel module accepts parameters at load time via ``insmod``
or persistently via ``/etc/modprobe.d/``.

**Load-time example:**

.. code-block:: console

   $ sudo insmod data_dev.ko cfgTxCount=2048 cfgRxCount=2048 cfgSize=262144

**Persistent configuration** (survives reboots when using ``modprobe``):

.. code-block:: console

   # /etc/modprobe.d/datadev.conf
   options datadev cfgTxCount=2048 cfgRxCount=2048 cfgSize=262144

.. note::

   All parameters have permission ``0`` — they are not readable from
   ``/sys/module/datadev/parameters/`` after the module is loaded.
   Use ``/proc/datadev_N`` to inspect the running configuration.

.. list-table::
   :header-rows: 1
   :widths: 28 16 56

   * - Parameter
     - Default
     - Description
   * - ``cfgTxCount``
     - ``1024``
     - Number of TX DMA buffers allocated at module load. Increase for
       high-throughput write workloads at the cost of memory.
   * - ``cfgRxCount``
     - ``1024``
     - Number of RX DMA buffers allocated at module load. Increase for
       high-throughput read workloads at the cost of memory.
   * - ``cfgSize``
     - ``0x20000`` (131072 = 128 kB)
     - Size in bytes of each DMA buffer. Must be a power of two.
       Larger values reduce ioctl overhead for large transfers;
       smaller values reduce memory waste for small transfers.
   * - ``cfgMode``
     - ``1`` (``BUFF_COHERENT``)
     - DMA buffer allocation mode. Valid values:
       ``1`` = ``BUFF_COHERENT`` (``dma_alloc_coherent``, CPU-coherent, default);
       ``2`` = ``BUFF_STREAM`` (``kmalloc`` + ``dma_map_single``, streaming);
       ``4`` = ``BUFF_ARM_ACP`` (ARM ACP path, RCE variants only).
   * - ``cfgCont``
     - ``1``
     - RX continue enable. ``1`` = driver automatically returns received buffers
       to the free pool after the application reads them; ``0`` = application
       must call ``dmaRetIndex()`` explicitly.
   * - ``cfgIrqHold``
     - ``10000``
     - Interrupt coalescing holdoff in hardware clock cycles. Higher values
       reduce interrupt rate at the cost of latency. Set to ``0`` to disable
       coalescing.
   * - ``cfgIrqDis``
     - ``0``
     - Interrupt disable flag. ``1`` = disable hardware interrupts and use
       polling mode. Use with caution: polling consumes a CPU core.
   * - ``cfgBgThold0``
     - ``0``
     - Buffer group threshold for channel group 0. ``0`` = no threshold enforced.
   * - ``cfgBgThold1``
     - ``0``
     - Buffer group threshold for channel group 1.
   * - ``cfgBgThold2``
     - ``0``
     - Buffer group threshold for channel group 2.
   * - ``cfgBgThold3``
     - ``0``
     - Buffer group threshold for channel group 3.
   * - ``cfgBgThold4``
     - ``0``
     - Buffer group threshold for channel group 4.
   * - ``cfgBgThold5``
     - ``0``
     - Buffer group threshold for channel group 5.
   * - ``cfgBgThold6``
     - ``0``
     - Buffer group threshold for channel group 6.
   * - ``cfgBgThold7``
     - ``0``
     - Buffer group threshold for channel group 7.
   * - ``cfgDevName``
     - ``0``
     - Device naming scheme. ``0`` = sequential (``datadev_0``, ``datadev_1``, ...);
       any nonzero value = PCI bus-number-based naming
       (``datadev_1a``, ``datadev_1b``, ...). Use nonzero when probe order is
       non-deterministic and stable names are required.
   * - ``cfgTimeout``
     - ``0xFFFF`` (65535)
     - Internal DMA transfer timeout duration in hardware clock cycles.
       Transfers that exceed this duration are aborted and flagged as errors.
   * - ``cfgDebug``
     - ``0``
     - Enables very verbose kernel log output. ``1`` = verbose (use only for
       debugging — generates large log volume under load).
