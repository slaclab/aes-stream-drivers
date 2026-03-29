Kernel Compatibility
====================

The aes-stream-drivers codebase targets Linux kernels from 3.10 through the current 6.8+ series.
This range spans major kernel API changes: the /proc interface was restructured, DMA mapping APIs
evolved, and several subsystem function signatures changed across that period. Rather than
requiring a specific kernel version, the driver uses ``LINUX_VERSION_CODE`` compile-time guards
to select the correct API call for each kernel generation.

How the Guards Work
--------------------

The standard Linux kernel macros ``LINUX_VERSION_CODE`` and ``KERNEL_VERSION(major, minor,
patch)`` are used throughout the common driver source. A typical guard looks like:

.. code-block:: c

   #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
       /* use old file_operations.proc_fops pattern */
   #else
       /* use proc_ops struct (introduced in 5.6) */
   #endif

No custom abstraction layer wraps these guards. Each guard is local to the file and function
where the API difference appears.

Compatibility Matrix
---------------------

The following guards are present in the driver source. All were verified by direct inspection of
the source files listed.

``< KERNEL_VERSION(2, 6, 25)`` — ``dma_common.c``
   ``ioremap_nocache`` is used in place of ``ioremap`` for non-cacheable register mapping. The
   ``nocache`` variant was removed in 2.6.25.

``< KERNEL_VERSION(3, 10, 0)`` — ``dma_common.c``
   Old /proc API: ``PDE(inode)->data`` is used to retrieve the private pointer from a /proc
   inode. This was replaced by ``PDE_DATA()`` in 3.10.

``< KERNEL_VERSION(4, 16, 0)`` — ``dma_common.h``
   The ``__poll_t`` type did not exist before 4.16. The driver provides a manual
   ``typedef unsigned int __poll_t`` for older kernels.

``< KERNEL_VERSION(5, 6, 0)`` — ``dma_common.c``
   The ``proc_ops`` struct was introduced in 5.6 to replace embedding function pointers
   directly in ``file_operations`` for /proc entries.

``< KERNEL_VERSION(5, 15, 0)`` — ``dma_buffer.c``
   DMA mapping API changes in 5.15 affected how streaming DMA buffers are managed.

``>= KERNEL_VERSION(5, 15, 0)`` — ``rce_top.c``
   An RCE-specific compatibility change for the same DMA API evolution.

``>= KERNEL_VERSION(5, 17, 0)`` or RHEL 9.3 — ``dma_common.c``
   ``pde_data()`` (lowercase) replaces the older ``PDE_DATA()`` macro. Red Hat backported this
   change into RHEL 9.3 with an older kernel version number, requiring explicit
   ``RHEL_RELEASE_CODE`` detection.

``>= KERNEL_VERSION(6, 0, 0)`` — Yocto ``axistreamdma.c``
   Coherent DMA memory allocation is configured via the device tree ``dma-coherent`` attribute
   instead of a build-time define.

``>= KERNEL_VERSION(6, 4, 0)`` or RHEL 9.4 — ``dma_common.c``, ``aximemorymap``
   ``class_create()`` dropped its first argument (``THIS_MODULE``) in 6.4. Red Hat backported
   this change in RHEL 9.4.

RHEL Backport Detection
------------------------

Red Hat Enterprise Linux backports upstream API changes into kernel versions whose
``LINUX_VERSION_CODE`` predates the upstream introduction. For example, RHEL 9.3 ships with a
5.14.x-based kernel but includes the ``pde_data()`` change from upstream 5.17. The driver
detects this using the ``RHEL_RELEASE_CODE`` macro alongside ``LINUX_VERSION_CODE`` guards.

This means the driver's compatibility range is not simply "kernels 3.10–6.8" but more
accurately "any kernel whose symbol set matches the guards above, including RHEL backport
variants."

Minimum and Maximum Tested Kernels
------------------------------------

The minimum supported kernel is **3.10** — this is the version that introduced the ``PDE_DATA()``
macro used in the /proc interface code. Kernels older than 3.10 are not supported. The driver is
tested in CI through **kernel 6.8+** (current as of this writing). Kernels beyond 6.8 may
require new guards if further API changes occur.

Further Reading
----------------

- Driver architecture and hardware abstraction: :doc:`architecture`
- Yocto integration (where the 6.0+ guard appears): :doc:`../how-to/yocto-integration`
