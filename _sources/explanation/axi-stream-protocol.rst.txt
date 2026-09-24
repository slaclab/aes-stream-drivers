AXI Stream Protocol
===================

AXI4-Stream is an AMBA bus protocol for unidirectional, handshake-based data transfer between IP
cores on an FPGA. The aes-stream-drivers project uses AXI4-Stream as the transport layer between
FPGA firmware and the DMA engine. Understanding AXI stream framing — specifically TLAST, TUSER,
and TDEST — is essential for writing correct firmware interfaces and userspace applications.

Frame Boundaries and TLAST
---------------------------

In AXI stream, a "frame" is a sequence of data beats terminated by the assertion of the TLAST
signal. When the FPGA firmware asserts TLAST on a data beat, the DMA engine marks the current
receive buffer as complete and places it in the software receive queue. A ``dmaRead`` or
``dmaReadIndex`` call will then return this buffer to userspace. Without TLAST, the DMA engine
continues filling the same buffer.

Large logical frames that exceed a single DMA buffer are handled with the ``cont`` flag (see
below). The DMA engine splits the logical frame across multiple physical buffers; the ``cont``
bit in the returned flags tells userspace that more buffers belong to the same logical frame.

The Flags Field: fuser, luser, and cont
-----------------------------------------

The driver represents per-transfer metadata in a 32-bit flags word. The bit layout, defined in
``AxisDriver.h``, is:

.. code-block:: c

   /* AxisDriver.h -- axisSetFlags bit layout */
   /* bits [7:0]  = fuser: first user (application-defined, carried on TUSER at first beat) */
   /* bits [15:8] = luser: last user  (application-defined, carried on TUSER at TLAST beat) */
   /* bit  [16]   = cont:  continuation (1 = more buffers belong to same logical frame)     */

The helper functions for packing and unpacking this flags word are:

.. code-block:: c

   uint32_t axisSetFlags(uint32_t fuser, uint32_t luser, uint32_t cont);
   uint32_t axisGetFuser(uint32_t flags);   /* returns flags & 0xFF */
   uint32_t axisGetLuser(uint32_t flags);   /* returns (flags >> 8) & 0xFF */
   uint32_t axisGetCont(uint32_t flags);    /* returns (flags >> 16) & 0x1 */

The ``fuser`` and ``luser`` fields are application-defined values carried in the AXI stream
TUSER sideband signal. Their interpretation is firmware-specific. Common uses include frame type
identifiers, sequence numbers, and error flags. The driver does not interpret these values — it
passes them transparently between the FPGA and userspace.

When a logical frame is too large for a single DMA buffer, the DMA engine sets the ``cont`` flag
on all buffers except the last. Userspace code that needs to reassemble multi-buffer frames must
collect buffers with ``cont=1`` and treat the first buffer where ``cont=0`` as the end of the
logical frame.

Channel Routing via TDEST
--------------------------

The ``dest`` parameter in ``dmaWrite`` and ``dmaRead`` maps directly to the AXI stream ``TDEST``
field. The DMA engine routes each frame to the physical channel identified by its TDEST value.
The driver supports up to ``DMA_MAX_DEST = 4096`` distinct channels (0 to 4095), as defined by
``8 * DMA_MASK_SIZE`` in ``dma_common.h``.

On the receive side, a file descriptor receives frames only if the frame's TDEST matches the
descriptor's channel mask. See :doc:`../how-to/multi-channel` for how to configure the mask.

AxisG2 Engine Overview
-----------------------

The AxisG2 IP core used in data_dev implements a ring buffer descriptor-based DMA engine. Key
registers include write and read base addresses (for Tx and Rx descriptor rings), interrupt
enable, FIFO reset, maximum transfer size, channel count, address width, and cache configuration.
The engine is initialized by ``AxisG2_Init`` (via the ``hardware_functions.init`` vtable entry)
on module load.

The AxisG2 engine is a SLAC-developed IP core. Its register layout is defined in
``data_dev/driver/src/axis_gen2.h``.

Further Reading
----------------

- Channel mask configuration: :doc:`../how-to/multi-channel`
- AXI stream API reference (axisSetFlags, axisGetFuser, etc.): :doc:`../reference/axis-api`
- Driver architecture and vtable: :doc:`architecture`
