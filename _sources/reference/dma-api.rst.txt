DMA Driver API
==============

Complete C API reference for the DMA driver, extracted from
``include/DmaDriver.h`` via Doxygen and rendered by Breathe.

Includes all public structs (``DmaWriteData``, ``DmaReadData``,
``DmaRegisterData``), ioctl command code macros, and the full set of
userspace inline helper functions such as ``dmaWrite``, ``dmaRead``,
``dmaMapDma``, and ``dmaUnMapDma``.

.. note::

   All helper functions are ``static inline`` and are compiled directly
   into the calling application. They are not shared library symbols.

.. doxygenfile:: DmaDriver.h
   :project: aes-stream-drivers
