Test Applications
=================

The ``data_dev/app/`` directory contains C++ test applications that exercise
the ``datadev`` kernel driver through its user-space API. These applications
form the verification backbone of the CI pipeline: every push to the
repository runs the full suite across five Linux distributions on real
kernel modules loaded against the emulator.

All applications link against ``include/DmaDriver.h``, ``include/AxisDriver.h``,
and the PRBS library (``data_dev/app/src/PrbsData.cpp``). They are built by
``make -C data_dev/app`` and placed in ``data_dev/app/bin/``.


DMA Loopback and Throughput
---------------------------

dmaLoopTest
^^^^^^^^^^^

Continuous DMA loopback with PRBS data integrity verification. A write
thread sends frames through the driver, the emulator ``memcpy``-loops them
back, and a read thread validates every byte against the expected PRBS
sequence.

.. code-block:: text

   Usage: dmaLoopTest -p <device> [-m <dest>] [-s <size>] [-d] [-i]
                      [-f <fuser>] [-l <luser>] [-t <pause_us>] [-r <txDis>]

   -p   Device path (e.g. /dev/datadev_0)
   -m   Destination channel (default: 0)
   -s   Frame size in bytes (default: 10000)
   -d   Disable PRBS checking
   -i   Enable index-based zero-copy mode
   -f   First-user flag (hex, written to fuser field)
   -l   Last-user flag (hex, written to luser field)
   -t   TX inter-frame pause in microseconds
   -r   Disable TX (receive-only mode)

Reports per-second statistics: TxCount, RxCount, TxRate, RxRate, PrbErr.
Used by CI in two modes: 30-second sustained throughput (Test 1) and
60-second data integrity check (``test_data_integrity.sh``).

dmaRate
^^^^^^^

Multi-threaded DMA throughput benchmark. Measures raw transfer rate with
configurable iteration count.

.. code-block:: text

   Usage: dmaRate -p <device> [-c <count>]

   -p   Device path
   -c   Number of iterations (default: 1000)


Ioctl and File Operations
-------------------------

dmaIoctlTest
^^^^^^^^^^^^^

Exercises all 28 ioctl commands (24 DMA from ``DmaDriver.h``, 2 AXIS from
``AxisDriver.h``, 1 AxiVersion from ``AxiVersion.h``, 1 GPU readiness from
``GpuAsync.h``), validating return values and argument semantics. The 27
numbered checks below span 28 ioctls because check 5 exercises both
``DMA_Get_Index`` and ``DMA_Ret_Index`` in one round-trip.

.. code-block:: text

   Usage: dmaIoctlTest -p <device>

Tests each ioctl in the following order:

.. list-table::
   :header-rows: 1
   :widths: 5 30 65

   * - #
     - Ioctl
     - Validation
   * - 1
     - ``DMA_Get_Buff_Count``
     - Returns > 0
   * - 2
     - ``DMA_Get_Buff_Size``
     - Returns > 0
   * - 3
     - ``DMA_Set_Debug``
     - Returns 0
   * - 4
     - ``DMA_Set_Mask``
     - Returns 0
   * - 5
     - ``DMA_Get_Index``
     - Returns >= 0
   * - 6
     - ``DMA_Ret_Index``
     - Returns 0 for previously obtained index
   * - 7
     - ``DMA_Read_Ready``
     - Returns >= 0
   * - 8
     - ``DMA_Set_MaskBytes``
     - Returns 0
   * - 9
     - ``DMA_Get_Version``
     - Returns 0x6 (API version)
   * - 10
     - ``DMA_Write_Register``
     - Returns 0
   * - 11
     - ``DMA_Read_Register``
     - Returns >= 0
   * - 12-22
     - Buffer counters (TX/RX in User/HW/PreHWQ/SWQ/Miss)
     - Each returns >= 0
   * - 23
     - ``DMA_Get_GITV``
     - Returns non-empty string
   * - 24
     - ``AXIS_Read_Ack``
     - Void return, no crash
   * - 25
     - ``AXIS_Write_ReqMissed``
     - Returns >= 0
   * - 26
     - ``AVER_Get``
     - Returns 0, firmware version non-zero
   * - 27
     - ``GPU_Is_Gpu_Async_Supp``
     - Returns 0 (CPU) or 1 (GPU)

dmaFileOpsTest
^^^^^^^^^^^^^^

Validates the driver's ``file_operations`` implementation: open, close,
multiple concurrent opens, ``select()`` read/write readiness, ``mmap`` for
DMA buffer access, and ``read()`` with no pending data.

.. code-block:: text

   Usage: dmaFileOpsTest -p <device>

Tests: open, multiple open, select(read), select(write), dmaMapDma,
dmaUnMapDma, read(no-data), ioctl(DMA_Get_Buff_Size), close.


Error Path Testing
------------------

dmaErrorTest
^^^^^^^^^^^^

Validates driver behavior under error conditions: buffer pool exhaustion,
oversized write rejection, and invalid buffer index handling.

.. code-block:: text

   Usage: dmaErrorTest -p <device>

Tests:

1. **Buffer exhaustion** --- holds all TX buffers, verifies ``dmaGetIndex``
   returns -1 when the pool is empty and ``DMA_Get_TxBuffinUser_Count``
   matches the held count.
2. **Oversized write** --- attempts a write larger than ``cfgSize``, expects
   rejection (``errno = EINVAL``).
3. **Invalid index** --- returns an out-of-range buffer index, expects
   rejection.

dmaSmallFrameTest
^^^^^^^^^^^^^^^^^

Sweep test for small frame sizes. Verifies that the DMA engine correctly
handles frames from a configurable minimum up to a maximum size, catching
alignment and boundary issues.

.. code-block:: text

   Usage: dmaSmallFrameTest -p <device> [-c <count>] [-n <min>] [-x <max>]

   -p   Device path
   -c   Frames per size step (default: 100)
   -n   Minimum frame size (default: 4)
   -x   Maximum frame size (default: 256)


GPU-Specific Testing
--------------------

These applications require the ``nvidia_p2p_stub`` and ``datadev_emulator``
modules loaded before ``datadev`` (GPU build). See :doc:`/how-to/gpudirect-setup`
for the full load sequence.

dmaGpuIoctlTest
^^^^^^^^^^^^^^^

Exercises all six GPU ioctl commands defined in ``GpuAsync.h``, validating
GPU async support detection, version queries, and buffer management.

.. code-block:: text

   Usage: dmaGpuIoctlTest -p <device>

Tests: ``GPU_Is_Gpu_Async_Supp``, ``GPU_Get_Gpu_Async_Ver``,
``GPU_Get_Max_Buffers``, ``GPU_Add_Nvidia_Memory``,
``GPU_Set_Write_Enable``, ``GPU_Rem_Nvidia_Memory``.

rdmaTestEmu
^^^^^^^^^^^

End-to-end GPUDirect RDMA loopback test using the emulator. A CUDA-free
C++17 port that allocates GPU-like memory via ``nvidia_p2p_stub``, registers
it with the driver, and performs DMA loopback transfers through the
emulator's GPU async V4 engine.

.. code-block:: text

   Usage: rdmaTestEmu [-d <device>] [-b <buffers>] [-s <size>]
                      [-c <count>] [-v] [--sweep]

   -d   Device path (default: /dev/datadev_0)
   -b   Number of GPU buffers (default: 1)
   -s   Transfer size in bytes (default: 4096)
   -c   Number of transfers (default: 10)
   -v   Verbose output (repeat for more)
   --sweep  Run payload-size matrix (powers of 2 up to 1 MB)

dmaGpuToggleTest
^^^^^^^^^^^^^^^^

Tests GPU enable/disable toggling via ioctl. Exercises the driver's
``GPU_Add_Nvidia_Memory`` / ``GPU_Rem_Nvidia_Memory`` cycle to verify
clean state transitions.

.. code-block:: text

   Usage: dmaGpuToggleTest -p <device>


Shell-Based Test Harness
------------------------

The ``tests/`` directory contains shell scripts that compose the C++
applications into higher-level integration tests. Each script is invoked
by ``scripts/ci/test-cpu.sh`` or ``scripts/ci/test-gpu.sh`` during CI.

.. list-table::
   :header-rows: 1
   :widths: 28 72

   * - Script
     - Coverage
   * - ``run_tests.sh``
     - Master test runner: ioctl, file-ops, error-paths, multichannel, proc,
       data-integrity, idx-loopback, tuser-sweep, frame-sizes, small-frames,
       concurrent-open, backpressure, irq-modes
   * - ``test_data_integrity.sh``
     - Runs ``dmaLoopTest`` for 60s, requires >= 100 transfers with zero
       PRBS mismatches
   * - ``test_multichannel.sh``
     - Simultaneous loopback on destinations 0, 7, and 8; verifies no
       cross-channel contamination
   * - ``test_backpressure.sh``
     - Saturates TX buffers then verifies recovery and correct dmesg state
   * - ``test_irq_modes.sh``
     - Sweeps the emulator's ``emu_irq_mode`` (``intx`` / ``msi`` /
       ``msix``). For each mode it reloads ``datadev`` across
       ``cfgIrqHold`` (1, 100000) and polled mode, verifies loopback +
       PRBS integrity, then asserts that ``datadev``'s probe-time
       ``pci_alloc_irq_vectors`` cascade selected the matching interrupt
       type. The ``emu_irq_mode`` sweep is CPU-phase only; the GPU phase
       and emulator-less hosts run the legacy single-mode pass
   * - ``test_concurrent_open.sh``
     - Two ``dmaLoopTest`` instances on different destinations simultaneously
   * - ``test_idx_loopback.sh``
     - Index-based zero-copy loopback (``dmaLoopTest -i``)
   * - ``test_frame_sizes.sh``
     - ``dmaLoopTest`` at 12 / 4096 / 32768 / 65536 bytes plus oversized
       (``cfgSize+1``) rejection assertion
   * - ``test_tuser_sweep.sh``
     - Two extreme fuser/luser combinations (0x00 + 0xFF, 0xFF + 0x00) via
       ``dmaLoopTest -f/-l`` to catch bit-shifting / masking bugs in
       axisSetFlags / axisGetFuser / axisGetLuser
   * - ``test_params.sh``
     - Module parameter validation: loads with custom cfgTxCount/cfgRxCount/cfgSize,
       verifies ``/proc/datadev_0`` reflects the new values
   * - ``test_proc.sh``
     - Validates ``/proc/datadev_0`` fields: buffer count, buffer size,
       Desc128En, IRQ, API version, buffer mode
   * - ``test_reload_segfault.sh``
     - Reproducer for the NULL-pointer deref in ``dmaAllocBuffers`` that
       fires when ``datadev`` is rebound in ``BUFF_COHERENT`` mode after a
       prior ``BUFF_STREAM`` cycle while the emulator stays loaded
   * - ``test_dma_rate.sh``
     - ``dmaRate`` throughput benchmark
   * - ``test_small_frames.sh``
     - ``dmaSmallFrameTest`` 1-4 byte payload sweep with random bytes and
       per-frame memcmp; exercises sub-word DMA transfers that PRBS
       validation cannot cover.
   * - ``test_gpu_ioctls.sh``
     - GPU ioctl validation via ``dmaGpuIoctlTest``
   * - ``test_gpu_dma_loopback.sh``
     - GPU DMA loopback: ``rdmaTestEmu --sweep`` payload sweep,
       10000-frame soak at 64 KiB, and ``dmaGpuToggleTest``
       (enable-toggle + maxBuffers 4→2 mid-stream reduction)
   * - ``test_gpu_proc.sh``
     - GPU-specific ``/proc`` field validation
