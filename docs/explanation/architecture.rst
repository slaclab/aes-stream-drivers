Driver Architecture
===================

The aes-stream-drivers project provides Linux kernel drivers for high-bandwidth DMA over PCIe
(data_dev) and AXI stream on Zynq/RCE (rce_stream). The design goal is a shared common layer
that handles the POSIX file interface, buffer management, and DMA housekeeping, while delegating
all hardware-specific behavior to a small vtable. This lets the same userspace API work across
PCIe, AXI, and future bus types without duplication.

The Hardware Abstraction Vtable
--------------------------------

The central abstraction in the driver is ``struct hardware_functions``, a C function pointer
table (vtable) defined in ``dma_common.h``. The common layer calls into this vtable for all
hardware-specific operations — interrupt handling, DMA engine initialization, buffer submission,
and register access — without knowing which hardware implementation it is calling.

.. code-block:: c

   /* dma_common.h */
   struct hardware_functions {
      irqreturn_t (*irq)(int irq, void *dev_id);
      void        (*init)(struct DmaDevice *dev);
      void        (*enable)(struct DmaDevice *dev);
      void        (*clear)(struct DmaDevice *dev);
      void        (*irqEnable)(struct DmaDevice *dev, int mask);
      void        (*retRxBuffer)(struct DmaDevice *dev, struct DmaBuffer **buff, uint32_t count);
      int32_t     (*sendBuffer)(struct DmaDevice *dev, struct DmaBuffer **buff, uint32_t count);
      int32_t     (*command)(struct DmaDevice *dev, uint32_t cmd, uint64_t arg);
      void        (*seqShow)(struct seq_file *s, struct DmaDevice *dev);
   };

The concrete instance for the PCIe/x86 data_dev hardware is ``DataDev_functions``, defined in
``data_dev_top.c``:

.. code-block:: c

   /* data_dev_top.c */
   struct hardware_functions DataDev_functions = {
      .irq          = AxisG2_Irq,
      .init         = AxisG2_Init,
      .clear        = AxisG2_Clear,
      .enable       = AxisG2_Enable,
      .irqEnable    = AxisG2_IrqEnable,
      .retRxBuffer  = AxisG2_RetRxBuffer,
      .sendBuffer   = AxisG2_SendBuffer,
      .command      = DataDev_Command,
      .seqShow      = DataDev_SeqShow,
   };

The RCE/ARM rce_stream driver provides a different instance pointing to ``AxisG1_*`` functions.
Both platforms share the entire common layer unchanged.

Component Relationships
------------------------

The driver is organized as a layered stack. Each layer has a well-defined interface to the layer
above and below it.

.. code-block:: text

   Userspace application
          | open/read/write/ioctl/mmap
          v
   DmaDesc (per-fd state: channel mask, rx queue)
          |
          v
   DmaDevice (per-card: buffer pools, config, hwFunc vtable)
          | .init/.irq/.sendBuffer/.retRxBuffer
          v
   hardware_functions vtable
          |
      +---+------------------+
   AxisG2_functions    AxisG1_functions
   (data_dev/PCIe)     (rce_stream/ARM)

Each layer's role:

- **DmaDesc**: created per ``open(2)`` call; holds the per-client channel mask and receive queue;
  destroyed on ``close(2)``
- **DmaDevice**: created per physical card at module load; holds the shared DMA buffer pools,
  hardware register mapping, and a pointer to the ``hardware_functions`` vtable
- **hardware_functions vtable**: the boundary between generic DMA logic and hardware-specific
  behavior; the common layer calls only through this interface

PCIe BAR0 Memory Map
---------------------

For the data_dev PCIe driver, BAR0 maps the following regions:

.. code-block:: c

   /* data_dev_top.h */
   #define AGEN2_OFF   0x00000000   /* DMAv2 Engine (64 kB) */
   #define PHY_OFF     0x00010000   /* PCIe PHY (64 kB) */
   #define AVER_OFF    0x00020000   /* AxiVersion (64 kB) */
   #define PROM_OFF    0x00030000   /* PROM (320 kB) */
   #define USER_OFF    0x00800000   /* User Space (8 MB) */

The DMAv2 engine at offset 0 is the AxisG2 IP core. The AxiVersion block at ``AVER_OFF``
(0x20000) provides firmware version and build information readable via ``/proc/datadev_N``.
The user space region at ``USER_OFF`` (0x800000) is application firmware — its layout is
design-specific and not interpreted by the driver.

PCI Device Identification
--------------------------

The driver binds to devices with PCI vendor ID ``0x1a4a`` (SLAC) and device ID ``0x2030``.
Use ``lspci -d 1a4a:2030`` to confirm the card is recognized by the kernel before loading the
module.

Further Reading
----------------

- DMA buffer allocation and zero-copy: :doc:`dma-modes`
- AXI stream channel routing: :doc:`axi-stream-protocol`
- DMA API function reference: :doc:`../reference/dma-api`
