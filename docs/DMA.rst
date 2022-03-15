==================
User Space DMA API
==================

This document describes the ``aes-stream-drivers`` user space API for DMA.

``include/DmaDriver.h``

- dmaWrite - Write the properties that describe a DMA transaction, initiating the transaction. Data to write is contained in ``buf``.
- dmaWriteIndex - Write the properties that describe a DMA transaction, initiating the transaction. Data to write is contained in a mapped buffer indexed by ``index``.
- dmaWriteVector - Write the properties that describe a sequence of DMA transactions, initiating the transactions. Data to write is contained in an iovec.
- dmaWriteIndexVector - Write the properties that describe a sequence of DMA transactions, initiating the transactions.  Data is write is contained in mapped buffers indexed by the data in the iovec.
- dmaRead - Read data from a device file. ``dest`` points to transferred data.
- dmaReadIndex - Read data from a device file. ``dest`` points to transferred data, which is contained in a mapped buffer.
- dmaReadBulkIndex - Read data from a device file.  ``dest`` points to a sequence of buffers.
- dmaRetIndex - Get a buffer associated with an index.
- dmaRetIndexes - Get buffers associated with indexes.
- dmaGetIndex - Get the index of a buffer that is available for a DMA transaction.
- dmaReadReady - Check if data is available to be read.
- dmaGetRxBuffCount - Get the total number of receive buffers.
- dmaGetTxBuffCount - Get the total number of transmit buffers.
- dmaGetBuffSize - Get the buffer size, measured in bytes.
- dmaMapDma - Allocate ``count`` buffers for DMA transactions, each measured in ``size`` bytes.
- dmaUnMapDma - Free all DMA buffers.
- dmaSetDebug - Enable printing information about API calls to the kernel ring buffer, viewable with dmesg.
- dmaAssignHandler - Assign a function to handle interrupts; called in a signal handler context (SIGIO).
- dmaSetMask - Reserve channel 0 for the application (convenience function).
- dmaInitMaskBytes - Initialize channel reservation request such that none would be requsted.
- dmaAddMaskBytes - Set the channel reservation request such that a channel would be requested, as defined by the application.
- dmaSetMaskBytes - Reserve multiple channels for the application, as defined by the mask bytes.
- dmaCheckVersion - Check that the kernel driver and user driver are compatible; returns 0 for success.
- dmaWriteRegister - Write to a device's register in I/O space.
- dmaReadRegister - Read from a device's register in I/O space.
- dmaMapRegister - Map a device's base address register (PCI BAR) in to the process space.
- dmaUnMapRegister - Unmap a device's base address register (PCI BAR) from the process space.

``include/AxisDriver.h``

- axisSetFlags - Set the flags for a DMA transfer.
- axisGetFuser - Get the file user bits associated with the DMA transer.
- axisGetLuser - Get the last user bits associated with the DMA transfer.
- axisGetCont - Get the continue bit; set when there is another DMA transfer.
- axisReadAck - Acknowledge that a DMA transfer has been completed by the application.
- axisWriteReqMissed - Read the count of DMA writes that failed to complete.
