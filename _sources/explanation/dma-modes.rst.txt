DMA Buffer Modes
================

The datadev driver pre-allocates a fixed pool of DMA buffers at module load time. The allocation
method — and the coherency properties of those buffers — is controlled by the ``cfgMode`` module
parameter. Understanding the trade-offs between buffer modes is important for tuning latency and
throughput.

The Three Buffer Modes
-----------------------

Three constants in ``dma_buffer.h`` define the available modes:

.. code-block:: c

   /* dma_buffer.h */
   #define BUFF_COHERENT  0x1   /* dma_alloc_coherent: CPU-device coherent at all times */
   #define BUFF_STREAM    0x2   /* dma_map_single: streaming, cache sync required */
   #define BUFF_ARM_ACP   0x4   /* ARM ACP port: coherent via ACP bus (RCE only) */

**BUFF_COHERENT (cfgMode=1):** Uses ``dma_alloc_coherent``. The kernel ensures CPU caches are
kept coherent with device memory at all times. This is the safest and most portable mode, and is
the default for data_dev. The trade-off is that coherent memory is typically non-cacheable on
x86, which adds a small latency penalty for CPU reads.

**BUFF_STREAM (cfgMode=2):** Uses ``dma_map_single``. Memory is cacheable; the driver must
explicitly call cache flush/invalidate operations before and after each DMA transfer
(``dma_sync_for_device`` before DMA, ``dma_sync_for_cpu`` after). This can improve throughput
when the CPU processes DMA buffers frequently, but requires correct cache synchronization to
avoid data corruption.

**BUFF_ARM_ACP (cfgMode=4):** Uses the ARM ACP (Accelerator Coherency Port), which provides
hardware cache coherency on certain ARM SoCs (Zynq/RCE). This mode is valid only for the
rce_stream driver. **The data_dev PCIe driver explicitly rejects cfgMode=4** — see below.

Why cfgMode=4 Is Rejected for data_dev
----------------------------------------

The ``data_dev_top.c`` probe function explicitly validates the mode parameter:

.. code-block:: c

   /* data_dev_top.c */
   if (cfgMode != BUFF_COHERENT && cfgMode != BUFF_STREAM) {
       pr_err("%s: Probe: Invalid buffer mode = %i.\n", MOD_NAME, cfgMode);
       return -EINVAL;
   }

The ARM ACP port is a hardware feature of the Xilinx Zynq SoC; it does not exist on x86 PCIe
systems. Passing ``cfgMode=4`` on an x86 ``insmod`` command will cause the driver probe to
return ``-EINVAL`` and the module load to fail immediately.

Buffer Pool Pre-Allocation
---------------------------

All DMA buffers are allocated during ``insmod`` via ``Dma_Init``. The count is set by
``cfgRxCount`` (receive pool) and ``cfgTxCount`` (transmit pool); the size per buffer is
``cfgSize`` bytes. No dynamic allocation occurs in the data path — this is a deliberate design
choice to eliminate allocation latency and fragmentation in real-time data acquisition scenarios.

Sizing the pool correctly at module load time is therefore important. If ``cfgRxCount`` is too
small for the expected burst rate, buffers will be exhausted and transfers will be dropped.

Buffer State Machine
---------------------

Each buffer is tracked by the ``DmaBuffer`` struct with three boolean flags:

.. code-block:: c

   /* dma_buffer.h */
   /* inHw: buffer is currently in DMA hardware (submitted to the engine) */
   /* inQ:  buffer is in the software receive queue, waiting for a user read */
   /* userHas != NULL: a userspace process holds this buffer index */
   /* all false: buffer is in the free pool, available for allocation */

The state transitions are: free pool → submitted to hardware (``inHw=1``) → software receive
queue (``inQ=1``) → user holds it (``userHas!=NULL``) → returned to free pool via
``dmaRetIndex``. A buffer that stays in the ``userHas`` state — because userspace never called
``dmaRetIndex`` — will drain the pool after ``cfgRxCount`` reads, causing the driver to stop
delivering data to that descriptor.

Zero-Copy Data Path
--------------------

The driver supports a zero-copy userspace interface via ``dmaMapDma``. This call uses ``mmap(2)``
to map each pre-allocated DMA buffer directly into userspace virtual address space, returning an
array of pointers. Subsequent ``dmaWriteIndex`` and ``dmaReadIndex`` calls name buffers by
integer index rather than copying data across the kernel/user boundary.

The zero-copy path is appropriate for high-throughput applications. The simple ``dmaWrite`` and
``dmaRead`` interface copies data through a kernel staging buffer; use the index-based interface
when minimizing CPU overhead matters.

Further Reading
----------------

- Load the driver with specific cfgMode: :doc:`../how-to/build-and-load`
- AXI stream channel routing and framing: :doc:`axi-stream-protocol`
- DMA API reference: :doc:`../reference/dma-api`
