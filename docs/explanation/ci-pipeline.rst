CI Pipeline and Test Coverage
==============================

The aes-stream-drivers CI pipeline validates every push across five Linux
distributions, two driver configurations (CPU and GPU), and multiple test
dimensions. The pipeline runs on GitHub Actions using the ``ubuntu-24.04``
runner with its ``6.17.0-xxxx-azure`` kernel, and can be reproduced locally
using KVM virtual machines.


Pipeline Phases
---------------

The CI pipeline (``ci_pipeline.yml``) executes five sequential phases, each
gating the next:

.. list-table::
   :header-rows: 1
   :widths: 8 22 70

   * - Phase
     - Name
     - Description
   * - 1
     - Documentation & Linting
     - Sphinx + Doxygen doc build, trailing-whitespace check, cpplint
       C/C++ lint, gpu_async.c change guard
   * - 2
     - CPU Testing
     - Build and test the CPU-only driver across 5 distros in parallel
   * - 3
     - GPU Testing
     - Build and test the GPU driver (with emulator + p2p stub) across
       5 distros in parallel
   * - 4
     - Release Generation
     - Creates GitHub Release artifacts (tags only)
   * - 5
     - DKMS Package Generation
     - Builds ``datadev-dkms-*.tar.gz`` and ``datadev-gpu-dkms-*.tar.gz``
       DKMS tarballs (tags only)


Distribution Matrix
-------------------

Both Phase 2 (CPU) and Phase 3 (GPU) run against six container images.
Each container runs ``--privileged`` with the host kernel's ``/lib/modules``
and ``/usr/src`` bind-mounted, so ``insmod`` loads against the live Azure
kernel regardless of the userspace distribution.

.. list-table::
   :header-rows: 1
   :widths: 22 12 12 54

   * - Distribution
     - CPU Load Test
     - GPU Load Test
     - Purpose
   * - ``ubuntu:26.04``
     - Yes
     - Yes
     - Primary platform; matches the GitHub Actions runner
   * - ``ubuntu:24.04``
     - No
     - No
     - LTS compatibility; build + DKMS smoke only
   * - ``ubuntu:22.04``
     - No
     - No
     - LTS compatibility; older glibc and toolchain; build + DKMS smoke only
   * - ``rockylinux:9``
     - Yes
     - Yes
     - RHEL 9 family; dnf package manager, different header layout
   * - ``debian:experimental``
     - Yes
     - Yes
     - Bleeding-edge packages; catches API deprecations early
   * - ``fedora:rawhide``
     - Yes
     - Yes
     - Rawhide gcc/glibc; most aggressive compiler warnings

Only the ``ubuntu:26.04`` cell — which matches the GitHub Actions runner
kernel — runs the full build + load + test + unload + DKMS sequence. The
``ubuntu:24.04`` and ``ubuntu:22.04`` cells set ``load_test: false`` and run
build + DKMS smoke only. The remaining distros keep ``load_test: true`` but
their load/test steps are gated at runtime on ``CI_HOST_MATCH=1`` (container
has the running host's kernel headers, either via bind-mount or package
install); distros that cannot satisfy that condition fall back to a DKMS
smoke path (``dkms ldtarball`` with ``--no-prepare-kernel``) in the same
cell.


Phase 2: CPU Test Coverage
--------------------------

Each CPU load-test cell executes the following sequence after building
the emulator, datadev driver, and test applications:

**Module loading** (``load-modules-cpu.sh``):

1. ``insmod nvidia_p2p_stub.ko``
2. ``insmod datadev_emulator.ko``
3. ``insmod datadev.ko cfgTxCount=64 cfgRxCount=64 cfgSize=65536 cfgDebug=1``
4. Verify ``/dev/datadev_0`` and ``/proc/datadev_0`` exist

**Test execution** (``test-cpu.sh``):

.. list-table::
   :header-rows: 1
   :widths: 5 25 70

   * - #
     - Test
     - What it validates
   * - 1
     - DMA loopback (30s)
     - Sustained bidirectional DMA throughput with PRBS integrity;
       random frame size per run (2000--20000 bytes)
   * - 2
     - Test suite (13 sub-tests)
     - Ioctl coverage, file operations, error paths, multi-channel
       routing, /proc interface, data integrity, index-based zero-copy,
       tuser flag sweep, frame size sweep, small frames (1-4 byte
       payload), concurrent opens, backpressure recovery, IRQ mode sweep
   * - 3
     - Module parameters
     - Reload with custom cfgTxCount=256/cfgRxCount=256/cfgSize=65536,
       verify /proc reflects the new values
   * - 4
     - cfgMode=2 reload
     - Unload/reload with BUFF_STREAM mode, run data integrity check
       (>= 100 transfers, zero PRBS errors)
   * - 5
     - rmmod-under-load
     - Start ``dmaLoopTest`` in background, ``rmmod datadev`` while DMA
       is active, verify no kernel oops or hang
   * - 6
     - Load/unload cycles
     - 3 rapid insmod/rmmod cycles to detect use-after-free races
   * - 7
     - DKMS
     - Build DKMS tarball, ``dkms ldtarball``, ``dkms install``, verify
       module is installed, ``dkms remove``

**Post-test** (``check-dmesg.sh``):

Baseline-delta dmesg analysis: compares kernel log after the test baseline
marker against known-benign patterns. Fails on any ``oops``, ``panic``,
``BUG:``, or ``WARNING:`` in the driver-induced delta.


Phase 3: GPU Test Coverage
--------------------------

Each GPU load-test cell executes the full CPU test suite **plus** GPU-specific
tests:

**Module loading** (``load-modules-gpu.sh``):

1. ``insmod nvidia_p2p_stub.ko``
2. ``insmod datadev_emulator.ko``
3. ``insmod datadev.ko`` (GPU build with ``NVIDIA_DRIVERS`` path)
4. Create ``/dev/nvidia_p2p_stub_mem`` miscdevice node

**Additional GPU tests** (``test-gpu.sh``):

.. list-table::
   :header-rows: 1
   :widths: 5 25 70

   * - #
     - Test
     - What it validates
   * - 1
     - GPU ioctl test
     - All 6 GPU ioctls: ``GPU_Is_Gpu_Async_Supp`` (returns 1),
       ``GPU_Get_Gpu_Async_Ver``, ``GPU_Get_Max_Buffers``,
       ``GPU_Add_Nvidia_Memory``, ``GPU_Set_Write_Enable``,
       ``GPU_Rem_Nvidia_Memory``
   * - 2
     - GPU proc interface
     - Validates GPU-specific fields in ``/proc/datadev_0``
   * - 3
     - GPU DMA loopback
     - ``rdmaTestEmu`` sweep + 10k-frame soak + ``dmaGpuToggleTest``
       (enable-toggle and max-buffers 4→2 mid-stream), all through the
       emulator's GPU Async V4 engine
   * - 4
     - GPU DKMS
     - Full DKMS build/install/remove cycle for GPU variant


Emulator Architecture
---------------------

All CI testing runs against the ``datadev_emulator`` kernel module, which
creates a virtual PCI device that the real ``datadev`` driver can probe
without physical FPGA hardware. This enables full end-to-end DMA testing
in any environment with a Linux kernel.

.. code-block:: text

   User Space          Kernel Space
   ──────────         ──────────────────────────────────────────
   dmaLoopTest   ──>  datadev.ko  ──>  DMA ring  ──>  datadev_emulator.ko
       ^                                                    |
       |                                                    | memcpy loopback
       +──────────────────────  DMA ring  <─────────────────+

The emulator provides:

- **Virtual PCI host bridge** with BAR0 register space
- **DMA engine** that captures TX descriptors from the read ring,
  ``memcpy``-loops the payload into an RX buffer, and writes RX completion
  descriptors to the write ring
- **PRBS generator** for data integrity seeding
- **GPU Async V4** register interface for GPU DMA testing
- **Virtual IRQ** (``virq``) for interrupt-driven processing

The emulator is hard-wired to 128-bit descriptor mode
(``Desc128En=1``) and handles the full ``AxisG2`` descriptor format
including fuser, luser, continuation, and multi-destination routing.
64-bit descriptor mode is **not** emulated and is not a supported
configuration for this project.

The ``enableVer`` register (BAR0 + ``0x0000``) mirrors the
``AxiStreamDmaV2Desc`` VHDL field layout:

.. list-table::
   :header-rows: 1
   :widths: 15 15 70

   * - Bits
     - Field
     - Access
   * - ``0``
     - ``enable``
     - R/W -- toggled by ``AxisG2_Enable`` / ``AxisG2_Clear``
   * - ``15:8``
     - ``enableCnt``
     - R/O -- counts 0→1 transitions of ``enable`` (driver load count)
   * - ``16``
     - ``Desc128En``
     - R/O constant, always ``1``
   * - ``31:24``
     - ``version``
     - R/O constant

Because BAR0 is backed by ordinary RAM, a naïve ``writel(0x0,
enableVer)`` from the driver's ``AxisG2_Clear`` path would zero the R/O
fields and make the next ``insmod`` read ``Desc128En=0`` / ``version=0``
-- silently disabling 128-bit completion processing.  The emulator's
DMA poll thread closes this reload hazard by re-asserting the R/O
fields on every cycle (``emu_enforce_enablever_ro()`` in
``dma_engine.c``): it preserves whatever bit 0 the driver just wrote,
increments ``enableCnt`` on a 0→1 edge, and rewrites the word with
``version`` and ``Desc128En`` reasserted.  The counter persists for the
lifetime of the emulator module, matching the VHDL's
load-counter semantics across driver reloads.


Emulator GPU Poll Thread
------------------------

The emulator's GPU Async engine is driven by a dedicated kthread
(``emu_gpu_poll`` in ``gpu_engine.c``) that mirrors what the FPGA
``AxiPcieGpuAsyncControl`` FSM does in hardware: it drains the free-list
and read-request slots, writes doorbells into GPU-side buffers, and
bumps ``rxFrameCnt`` / ``txFrameCnt``. Two aspects of how this kthread
is scheduled are load-bearing for CI reliability.

**Tick cadence (** ``emu_gpu_poll_interval_us`` **).** The kthread
paces itself with ``usleep_range`` between ticks. The module parameter
defaults to ``1000`` µs for developer workflow (cheap CPU cost, plenty
of margin for interactive testing). ``scripts/ci/load-modules-gpu.sh``
overrides this to ``100`` µs on every CI insmod so the kthread keeps up
with userspace's per-doorbell 10-second deadline during the 10 k-frame
soak. The param is read on every iteration, so sysfs late-binds without
a module reload.

**Scheduling class (SCHED_FIFO).** After ``kthread_run``, the poll
thread is promoted to ``SCHED_FIFO(1)`` via ``sched_set_fifo_low()``.
The test binary (``rdmaTestEmu``) busy-spins on a volatile doorbell
word in userspace; on a 2-vCPU Azure runner that spin can peg the CFS
share and defer the kthread's ``usleep_range`` wakeup for many seconds.
``SCHED_FIFO`` real-time tasks preempt CFS, so the wakeup is honored
promptly regardless of how much time userspace is burning. Priority 1
("fifo_low") is low enough that the kernel's own critical RT tasks
still outrank it. The ``usleep_range`` inside the loop remains — it is
the CPU-relief valve that keeps the kthread from pinning a core.

Both mechanisms matter. Without the tighter poll interval, a cold CFS
wakeup can drift several hundred microseconds between ticks and never
catch up to the soak's throughput. Without ``SCHED_FIFO``, a contended
vCPU lets that drift balloon into the 10-second doorbell budget.
Together they keep the soak green across all five CI distributions
including fedora:rawhide and ubuntu:22.04, which were previously
intermittently failing with ``rdmaTestEmu: rx doorbell timeout`` on
the GHA runner's nested-KVM scheduler.


DKMS Packaging
--------------

The pipeline produces two DKMS tarballs for distribution:

- ``datadev-cpu-<version>.tar.gz`` --- CPU-only driver (no NVIDIA dependency)
- ``datadev-gpu-<version>.tar.gz`` --- GPU driver (requires NVIDIA kernel modules)

Both are tested via ``dkms ldtarball`` / ``dkms install`` / ``dkms remove``
in CI. On tagged releases, the tarballs are uploaded to the GitHub Release page.


Local CI Testing
----------------

The CI pipeline can be reproduced locally using KVM virtual machines. This
is essential for iterating on kernel module changes before pushing to GitHub.

**Single-cell validation** (fastest feedback loop):

.. code-block:: bash

   bash scripts/ci-local/run_cell.sh \
      --container ubuntu:24.04 --load-test 1 --phase cpu

**Full matrix** (sequential, one VM):

.. code-block:: bash

   bash scripts/ci-local/run_matrix.sh --phase cpu
   bash scripts/ci-local/run_matrix.sh --phase gpu

**Parallel matrix** (one VM per cell, requires multi-core host):

.. code-block:: bash

   export AES_CI_PARALLEL_VM_MEMORY=3072
   export AES_CI_PARALLEL_VM_VCPUS=2
   bash scripts/ci-local/run_matrix.sh --phase cpu --parallel
   bash scripts/ci-local/run_matrix.sh --phase gpu --parallel

Each local KVM runs the same Azure kernel family as the GitHub Actions
runner, the same Docker container images, and the same ``scripts/ci/*.sh``
test scripts. Results are directly comparable.

See ``scripts/ci-local/AI_LOCAL_CI_TESTING.md`` for the complete local
CI reference, including VM provisioning, virtiofs repo sync, and
diagnostic workflows.


Test Coverage Summary
---------------------

The combined Phase 2 + Phase 3 coverage across all distributions:

.. list-table::
   :header-rows: 1
   :widths: 30 15 55

   * - Category
     - Tests
     - Coverage
   * - DMA data path
     - 5
     - Loopback, throughput, PRBS integrity, small frames, index-based
       zero-copy
   * - Ioctl interface
     - 28
     - 24 DMA + 2 AXIS + 1 AxiVersion + 1 GPU readiness ioctls validated
   * - GPU ioctls
     - 6
     - All 6 GPU async ioctls including memory registration
   * - File operations
     - 8
     - open, multi-open, select(read), select(write), mmap, read, ioctl,
       close
   * - Error handling
     - 3
     - Buffer exhaustion, oversized write, invalid index
   * - Channel routing
     - 3
     - Multi-destination (0, 7, 8), cross-contamination check
   * - IRQ modes
     - 12
     - ``emu_irq_mode`` {intx, msi, msix} sweep (CPU phase). Each mode
       runs cfgIrqHold=1, cfgIrqHold=100000, and polled (cfgIrqDis=1),
       plus a probe-cascade assertion that ``datadev`` selected the
       matching INTx / MSI / MSI-X path. GPU phase runs the single-mode
       legacy pass
   * - Module lifecycle
     - 4
     - 3 rapid reload cycles, rmmod-under-load, cfgMode=1/2 transitions
   * - Module parameters
     - 3
     - Custom buffer counts, custom sizes, /proc reflection
   * - Buffer modes
     - 2
     - BUFF_COHERENT (cfgMode=1), BUFF_STREAM (cfgMode=2)
   * - DKMS packaging
     - 2
     - CPU build/install/remove, GPU build/install/remove (full
       tarball cycle on every distro with matching kernel headers;
       ``dkms ldtarball`` smoke fallback when headers can't be matched)
   * - GPU DMA loopback
     - 3
     - ``rdmaTestEmu --sweep`` payload sweep, 10000-frame soak at 64 KiB,
       ``dmaGpuToggleTest`` (enable-toggle + maxBuffers 4→2 mid-stream
       reduction). Mirrors the test_gpu_dma_loopback.sh subtest list.
   * - Concurrent access
     - 2
     - Two-process loopback, backpressure/recovery
   * - AXI stream flags
     - 2
     - Two extreme fuser/luser combinations (0x00 + 0xFF, 0xFF + 0x00)
       to catch bit-shifting / masking bugs in axisSetFlags /
       axisGetFuser / axisGetLuser
   * - /proc interface
     - 9
     - Buffer count, size, Desc128En, IRQ, API version, buffer mode,
       GPU fields, buffer states
   * - Documentation
     - 3
     - Sphinx build, Doxygen XML, HTML output validation
   * - Code quality
     - 3
     - Trailing whitespace, tab detection, cpplint

**Total: ~95 individual test checks across 5 distributions x 2 phases = 10 CI cells.**
