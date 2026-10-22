==================
User Space DMA API
==================

This page summarizes the ``aes-stream-drivers`` user space API for DMA. For the complete
reference generated from the headers, see :doc:`dma-api` and :doc:`axis-api`.

``include/DmaDriver.h``

- dmaWrite - Describe a DMA transaction and initiate it. The data to write is contained in ``buf``.
- dmaWriteIndex - Describe a DMA transaction and initiate it. The data to write is contained in the mapped buffer selected by ``index``.
- dmaWriteVector - Describe a sequence of DMA transactions and initiate them. The data to write is contained in an iovec.
- dmaWriteIndexVector - Describe a sequence of DMA transactions and initiate them. The data to write is contained in the mapped buffers indexed by the iovec entries.
- dmaRead - Read one frame into ``buf``; the frame's flags, error status, and destination channel are returned through ``flags``, ``error``, and ``dest``.
- dmaReadIndex - Read one frame without copying; the mapped buffer holding it is returned through ``index``, along with its flags, error status, and destination.
- dmaReadBulkIndex - Read multiple frames in one call, returning arrays of frame sizes, buffer indexes, flags, errors, and destinations.
- dmaRetIndex - Return a buffer index to the driver after processing.
- dmaRetIndexes - Return multiple buffer indexes to the driver in one call.
- dmaGetIndex - Get the index of a transmit buffer that is available for a DMA transaction.
- dmaReadReady - Check if data is available to be read.
- dmaGetRxBuffCount - Get the total number of receive buffers.
- dmaGetTxBuffCount - Get the total number of transmit buffers.
- dmaGetBuffSize - Get the buffer size, measured in bytes.
- dmaMapDma - Map all DMA buffers into the process space; returns an array of buffer pointers and reports the buffer count and size.
- dmaUnMapDma - Unmap the DMA buffers mapped by ``dmaMapDma``.
- dmaSetDebug - Enable printing information about API calls to the kernel ring buffer, viewable with dmesg.
- dmaAssignHandler - Assign a function to handle interrupts; called in a signal handler context (SIGIO).
- dmaSetMask - Claim channels 0 through 31 for the descriptor, using a 32-bit bitmask (bit *N* = channel *N*).
- dmaInitMaskBytes - Initialize a channel mask so that no channels are requested.
- dmaAddMaskBytes - Add a channel to a channel mask.
- dmaSetMaskBytes - Claim every channel set in the mask for the descriptor.
- dmaCheckVersion - Check that the kernel driver and user driver are compatible; returns 0 for success.
- dmaWriteRegister - Write to a device's register in I/O space.
- dmaReadRegister - Read from a device's register in I/O space.
- dmaMapRegister - Map a device's base address register (PCI BAR) into the process space.
- dmaUnMapRegister - Unmap a device's base address register (PCI BAR) from the process space.

``include/AxisDriver.h``

- axisSetFlags - Pack the first-user, last-user, and continuation fields into a flags word for a DMA transfer.
- axisGetFuser - Get the first-user bits associated with the DMA transfer.
- axisGetLuser - Get the last-user bits associated with the DMA transfer.
- axisGetCont - Get the continuation bit; set when the frame continues into another buffer.
- axisReadAck - Acknowledge a read by writing the DMA engine's acknowledge register.
- axisWriteReqMissed - Issue the ``AXIS_Write_ReqMissed`` ioctl. The driver returns the engine's missed write-request count, but this ``void`` wrapper discards it; call ``ioctl()`` directly to read the count.
