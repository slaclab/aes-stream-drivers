GPU Async API
=============

Complete user-space API reference for GPU asynchronous (GPUDirect RDMA)
support, extracted from ``include/GpuAsync.h`` (low-level ioctl
wrappers) and ``include/GpuAsyncUser.h`` (C++ context and lifecycle
API) via Doxygen and rendered by Breathe.

.. note::

   All ioctl wrappers and lifecycle functions are ``static inline`` and
   are compiled directly into the calling application. They are not
   shared library symbols.

   The caller must call ``cuInit(0)`` before calling ``gpuAsyncInit``.

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

Context and Lifecycle API (GpuAsyncUser.h)
------------------------------------------

C++11 API that bundles the full GPU/FPGA bring-up and tear-down
sequence — buffer validation, CUDA context and stream creation,
register mapping, free-list arming, and strict reverse-order teardown —
into a single ``GpuAsyncContext`` struct managed by ``gpuAsyncInit``
and ``gpuAsyncCleanup``. Also provides ``GpuAsyncCoreRegs``, a thin
wrapper over the mapped register block that hides offset and behaviour
differences between V1 and V4 firmware.

.. doxygenfile:: GpuAsyncUser.h
   :project: aes-stream-drivers
