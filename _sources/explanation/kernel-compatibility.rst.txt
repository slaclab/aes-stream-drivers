Kernel Compatibility
====================

The aes-stream-drivers codebase targets Linux kernels from 3.10 through the current 6.x series.
This range spans major kernel API changes: the /proc interface was restructured, DMA mapping APIs
evolved, and several subsystem function signatures changed across that period. Rather than
requiring a specific kernel version, the driver uses ``LINUX_VERSION_CODE`` compile-time guards
to select the correct API call for each kernel generation.

How the Guards Work
-------------------

The standard Linux kernel macros ``LINUX_VERSION_CODE`` and ``KERNEL_VERSION(major, minor,
patch)`` are used throughout the common driver source. A typical guard looks like:

.. code-block:: c

   #if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
       /* use proc_ops struct (introduced in 5.6) */
   #else
       /* use old file_operations pattern */
   #endif

No custom abstraction layer wraps these guards. Each guard is local to the file and function
where the API difference appears.

Compatibility Matrix
--------------------

The following guards are present in the driver source, listed by kernel version. Unless noted,
the files are under ``common/driver/``.

``< KERNEL_VERSION(3, 10, 0)``: ``dma_common.c``
   Old /proc API: ``PDE(inode)->data`` is used to retrieve the private pointer from a /proc
   inode. This was replaced by ``PDE_DATA()`` in 3.10.

``< KERNEL_VERSION(4, 16, 0)``: ``dma_common.h``
   The ``__poll_t`` type did not exist before 4.16. The driver provides its own
   ``typedef uint32_t __poll_t`` for older kernels.

``< KERNEL_VERSION(5, 0, 0)``: ``dma_common.c``
   ``access_ok()`` dropped its leading ``VERIFY_READ`` / ``VERIFY_WRITE`` type argument in 5.0.

``>= KERNEL_VERSION(5, 1, 0)``: ``axi_hwmon.c``
   The hwmon sensor interface (AxiVersion / SysMon) is compiled only on 5.1 and later; older
   kernels build empty stubs.

``>= KERNEL_VERSION(5, 6, 0)``: ``dma_common.c``
   The ``proc_ops`` struct replaces embedding function pointers directly in
   ``file_operations`` for /proc entries. The same release removed ``ioremap_nocache``, so
   register space is mapped with plain ``ioremap`` (which is already non-cached).

``>= KERNEL_VERSION(5, 15, 0)``: ``dma_buffer.c``
   ``BUFF_STREAM`` buffers are allocated with ``dma_alloc_noncoherent`` and released with
   ``dma_free_noncoherent``. Older kernels use ``kmalloc`` + ``dma_map_single`` instead.

``>= KERNEL_VERSION(5, 15, 0)``: ``rce_stream/driver/src/rce_top.c``
   The RCE driver includes ``<linux/dma-map-ops.h>`` for the DMA ops declarations it uses.

``>= KERNEL_VERSION(5, 17, 0)`` or RHEL 9.1: ``dma_common.c``
   ``pde_data()`` (lowercase) replaces the older ``PDE_DATA()`` macro. Red Hat backported this
   change into RHEL 9.1 with an older kernel version number, requiring explicit
   ``RHEL_RELEASE_CODE`` detection.

``>= KERNEL_VERSION(6, 0, 0)``: Yocto ``axistreamdma.c``
   Coherent DMA memory allocation is configured via the device tree ``dma-coherent`` attribute
   instead of a build-time define.

``>= KERNEL_VERSION(6, 1, 0)``: ``dma_common.c``
   ``min()`` / ``max()`` come from ``<linux/minmax.h>`` rather than ``<linux/kernel.h>``.

``>= KERNEL_VERSION(6, 2, 0)`` or RHEL 9.3: ``dma_common.h``, Yocto ``aximemorymap.h``
   The class ``devnode()`` callback takes a ``const struct device *``. Red Hat backported this
   change in RHEL 9.3.

``>= KERNEL_VERSION(6, 4, 0)`` or RHEL 9.4: ``dma_common.c``, Yocto ``aximemorymap.c``
   ``class_create()`` dropped its first argument (``THIS_MODULE``) in 6.4. Red Hat backported
   this change in RHEL 9.4.

``>= KERNEL_VERSION(6, 11, 0)``: Yocto ``axistreamdma.c`` / ``axistreamdma.h``
   The platform driver ``remove()`` callback returns ``void`` instead of ``int``.

``#ifndef PCI_IRQ_INTX``: ``data_dev_top.c``
   ``PCI_IRQ_LEGACY`` was renamed to ``PCI_IRQ_INTX`` in 6.11 (the old name was kept as a
   deprecated alias for a few releases, then dropped). Rather than a version check, a
   ``#ifndef PCI_IRQ_INTX`` guard defines the new spelling as ``PCI_IRQ_LEGACY`` so the single
   ``pci_alloc_irq_vectors()`` call site builds on pre-6.11 kernels (e.g. Ubuntu 22.04 with
   kernel 5.15, which lacks ``PCI_IRQ_INTX``).

RHEL Backport Detection
-----------------------

Red Hat Enterprise Linux backports upstream API changes into kernel versions whose
``LINUX_VERSION_CODE`` predates the upstream introduction. For example, RHEL 9.1 ships with a
5.14.x-based kernel but includes the ``pde_data()`` change from upstream 5.17. The driver
detects this using the ``RHEL_RELEASE_CODE`` macro alongside ``LINUX_VERSION_CODE`` guards.
On non-RHEL kernels, ``dma_common.h`` defines ``RHEL_RELEASE_VERSION(...)`` as ``0`` so the
combined conditions still compile.

This means the driver's compatibility range is not simply "kernels 3.10 through 6.x" but more
accurately "any kernel whose symbol set matches the guards above, including RHEL backport
variants."

Minimum and Maximum Tested Kernels
----------------------------------

The minimum supported kernel is **3.10**, the version that introduced the ``PDE_DATA()``
macro used in the /proc interface code. Kernels older than 3.10 are not supported. CI builds
and loads the driver against the GitHub Actions runner's kernel (the 6.17 Azure series at the
time of writing; see :doc:`ci-pipeline`). Newer kernels may require additional guards if
further API changes occur.

Further Reading
---------------

- Driver architecture and hardware abstraction: :doc:`architecture`
- Yocto integration (where the 6.0+ and 6.11+ guards appear): :doc:`../how-to/yocto-integration`
