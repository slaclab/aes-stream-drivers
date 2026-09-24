GPU Async API
=============

User-space API reference for GPU asynchronous (GPUDirect RDMA) support.
The interface is split across three headers in ``include/``:

* ``GpuAsync.h``     — low-level ioctl wrappers (C-callable).
* ``GpuAsyncUser.h`` — ``GpuAsyncCoreRegs``: a thin C++ wrapper over the
  mapped GpuAsyncCore register block. Hides offset and behaviour
  differences between V1 and V4 firmware.
* ``GpuAsyncLib.h``  — CUDA + GpuAsync helpers: ``DataGPU``,
  ``CudaContext``, ``GpuDmaBuffer_t``, ``gpuMapHostFpgaMem``,
  ``gpuMapFpgaMem``, paired ``GpuBufferState_t``, plus the
  ``AxiWrDesc64_t`` AXI stream descriptor.

.. note::

   The ioctl wrappers in ``GpuAsync.h`` are ``static inline`` and compile
   into the calling application. The ``GpuAsyncLib`` helpers
   (``DataGPU``, ``CudaContext``, ``gpuMapFpgaMem``, etc.) are declared
   in the header and defined in ``data_dev/app/src/GpuAsyncLib.cpp`` —
   link that translation unit into your application.

   The caller must call ``cuInit(0)`` before constructing any
   ``CudaContext``. The ``CudaContext`` constructor calls ``cuInit(0)``
   itself, so direct application use is straightforward.

Ioctl Wrappers (GpuAsync.h)
---------------------------

C wrappers around the GpuAsync ioctls (``GPU_Add_Nvidia_Memory``,
``GPU_Is_Gpu_Async_Supp``, ``GPU_Get_Gpu_Async_Ver``,
``GPU_Get_Max_Buffers``, etc.; see :doc:`ioctl-table`). Safe to use from
either C or C++ code. ``gpuGetGpuAsyncVersion`` and ``gpuGetMaxBuffers``
return ``ssize_t`` so kernel-level ioctl failures (``-1`` with ``errno``
set) are representable without wraparound; callers must check for a
negative return before using the value.

.. doxygenfile:: GpuAsync.h
   :project: aes-stream-drivers

Register Wrapper (GpuAsyncUser.h)
---------------------------------

C++11 wrapper class ``GpuAsyncCoreRegs`` over the mapped GpuAsyncCore
register block. The lifetime of a ``GpuAsyncCoreRegs`` instance must be
within the lifetime of the memory mapping it was constructed over.
Code calling into the class does not need to be aware of register
offsets or the underlying GpuAsyncCore version (V1 vs V4).

.. doxygenfile:: GpuAsyncUser.h
   :project: aes-stream-drivers

CUDA Helpers (GpuAsyncLib.h)
----------------------------

Thin C++ helpers for the boilerplate CUDA + GpuAsync setup:
``DataGPU`` (RAII for ``/dev/datadev_X``), ``CudaContext`` (cuInit /
device select / context create), ``GpuDmaBuffer_t`` and
``GpuBufferState_t`` (FPGA-mapped GPU memory), and the ``gpuMapFpgaMem``
/ ``gpuMapHostFpgaMem`` / ``gpuUnmapFpgaMem`` helpers. The library is
deliberately scope-narrow — it does not own a CUDA stream or prescribe
a session lifecycle, leaving stream / buffer-pool / arming policy to
the calling application.

.. doxygenfile:: GpuAsyncLib.h
   :project: aes-stream-drivers
