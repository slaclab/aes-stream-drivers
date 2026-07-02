AXIS Driver API
===============

Complete C API reference for the AXIS stream driver, extracted from
``include/AxisDriver.h`` via Doxygen and rendered by Breathe.

Includes the AXIS ioctl command code macros (``AXIS_Read_Ack``,
``AXIS_Write_ReqMissed``) and the flag-packing helper functions
``axisSetFlags``, ``axisGetFuser``, ``axisGetLuser``, ``axisGetCont``,
``axisReadAck``, and ``axisWriteReqMissed``.

.. note::

   ``AxisDriver.h`` includes ``DmaDriver.h`` internally. Functions such as
   ``dmaWrite`` and ``dmaRead`` are documented in :doc:`dma-api`.

.. doxygenfile:: AxisDriver.h
   :project: aes-stream-drivers
