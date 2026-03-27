# Testing Patterns

**Analysis Date:** 2026-03-26

## Test Framework

**Runner:** None — no automated test framework (no pytest, Google Test, Catch2, KUnit, etc.)

**Assertion Library:** None — correctness is verified by visual inspection of printed output and comparison of expected vs actual data patterns (PRBS integrity checks).

**Run Commands:**
```bash
# Build driver and apps together
make driver      # builds kernel module for all installed kernel versions
make app         # builds all userspace test applications

# Run rate test (requires loaded driver and hardware)
cd data_dev/app && make
bin/dmaRate --count=100000        # terminal 1: reads and measures throughput
bin/dmaWrite 0 --count=1000000    # terminal 2: sends data
```

## Test File Organization

There are no dedicated unit test files. "Tests" are functional application programs compiled as standalone binaries and run manually against real hardware.

**Application test source:**
```
common/app/
  dmaRead.cpp       # Reads DMA frames, validates PRBS, prints stats
  dmaWrite.cpp      # Writes DMA frames with configurable dest/flags/size
  dmaLoopTest.cpp   # Multi-threaded write+read loop test with PRBS checking
  dmaSetDebug.cpp   # Enables kernel debug logging via ioctl

common/app_lib/
  PrbsData.cpp      # PRBS generator/checker (shared library for test apps)
  PrbsData.h
  AppUtils.h        # Shared arg parsing and utility helpers

data_dev/app/src/
  test.cpp          # Minimal write benchmark (10,000 DMA writes, measures time)
  dmaRate.cpp       # Sustained read throughput measurement with latency stats
  rdmaTest.cu       # GPUDirect RDMA test (CUDA, requires NVIDIA hardware)
```

## Test Structure

### Application Design Pattern

All test applications follow the same structure:

```cpp
// 1. Parse args with argp
struct PrgArgs { ... };
static struct PrgArgs DefArgs = { "/dev/datadev_0", ... };
static struct argp_option options[] = { ... };
error_t parseArgs(int key, char *arg, struct argp_state *state) { ... }
static struct argp argp = {options, parseArgs, args_doc, doc};

// 2. Open device
int32_t s = open("/dev/datadev_0", O_RDWR);

// 3. Configure via ioctl (set mask, get buffer count, etc.)
dmaSetMaskBytes(s, mask);

// 4. Run operation loop
while (...) {
    dmaWrite(s, data, size, dest, flags);  // or dmaRead, dmaReadBulk, etc.
}

// 5. Print results / check PRBS integrity
// 6. close(s)
```

### PRBS Integrity Checking

`common/app_lib/PrbsData.cpp` implements a configurable LFSR-based PRBS (Pseudo-Random Binary Sequence) generator. Test apps (`dmaRead`, `dmaLoopTest`) call `prbs.processData(buffer, size)` on received frames to verify data integrity end-to-end. A mismatch indicates data corruption.

The writer side generates frames with `prbs.genData(buffer, size)` before transmitting.

### Rate / Throughput Testing

`data_dev/app/src/dmaRate.cpp` and the loop test in `common/app/dmaLoopTest.cpp` measure:
- Frames per second
- Bandwidth (bytes/sec)
- Read latency (microseconds)
- Return latency (microseconds)

Results are printed as a formatted table to stdout. Pass/fail is manual inspection — no assertions or exit codes for performance thresholds.

### Thread-Based Load Testing

`dmaLoopTest.cpp` spawns configurable numbers of write and read `pthread` threads to simulate concurrent multi-channel usage. Thread count and destination are command-line configurable.

## How to Test

### Step 1: Build

```bash
# On the target machine with the hardware installed
cd /path/to/aes-stream-drivers
make driver    # compiles datadev.ko
make app       # compiles test binaries
```

### Step 2: Load the Driver

```bash
# Insert the kernel module
sudo insmod data_dev/driver/datadev.ko

# Verify device nodes created
ls /dev/datadev_*

# Check kernel log for init messages
dmesg | tail -20
```

### Step 3: Run Functional Tests

**Basic write test:**
```bash
# data_dev/app
bin/test        # writes 10,000 frames, prints elapsed time and rate
```

**Throughput test (two terminals):**
```bash
# Terminal 1
bin/dmaRate --count=100000

# Terminal 2
bin/dmaWrite 0 --count=1000000
```

**Loop test with PRBS checking:**
```bash
common/app/dmaLoopTest --path=/dev/datadev_0 --dest=0 --size=10000
```

**Read test with PRBS validation:**
```bash
common/app/dmaRead --path=/dev/datadev_0 --dest=0 --prbsen
```

**GPU RDMA test (requires NVIDIA hardware and CUDA):**
```bash
cd data_dev/app && make cuda
sudo bin/rdmaTest -s 0x20000 -c 10 -vv
```

### Step 4: Inspect /proc Entry

Each device creates a `/proc/<devName>` entry with driver statistics:

```bash
cat /proc/datadev_0    # shows buffer counts, queue depths, hardware state
```

### Step 5: Debug Logging

Enable verbose kernel logging at runtime:

```bash
common/app/dmaSetDebug --path=/dev/datadev_0 --set=1
dmesg -w    # watch kernel log for per-frame debug output
```

**Note from README:** Do not leave debug enabled during performance tests — it causes IRQ handler overhead that significantly degrades throughput.

## CI/CD Pipeline

Pipeline defined in `.github/workflows/aes_ci.yml`, runs on every push.

### Jobs

**`test_and_document`** (ubuntu-24.04):
- Installs Python 3.12 and pip dependencies (`pip_requirements.txt`)
- Checks for trailing whitespace and tabs in `*.c`, `*.cpp`, `*.h`, `*.sh`, `*.py`
- Runs `cpplint` on all C/C++ source files

**`build`** (matrix across 5 containers):

| Container | Kernel(s) Tested |
|-----------|-----------------|
| `ubuntu:22.04` | 5.19, 6.5, 6.8 |
| `ubuntu:24.04` | 6.8 |
| `rockylinux:9` | 5.14 (RHEL9) |
| `debian:experimental` | Latest released |
| `ghcr.io/jjl772/centos7-vault` | 3.10 (RHEL7) |

Each build job:
1. Installs kernel headers for target distro
2. Compiles kernel module: `make driver`
3. Compiles userspace apps: `make app`
4. `debian:experimental` also runs:
   - `sparse` (`make driver C=2 CF=-Wsparse-error`)
   - `clang-tidy` via `bear -- make driver app` + `scripts/filter-clangdb.py` + `scripts/run-clang-tidy.py`

**`gen_release`**: Auto-generates GitHub releases (depends on `test_and_document`).

**`generate_dkms`**: Builds and uploads a DKMS tarball on tagged commits.

### What CI Does NOT Test

- No kernel module load/unload testing in CI (no hardware, no `insmod`)
- No functional DMA transfer tests in CI
- No PRBS data integrity checks in CI
- No concurrent access or stress tests in CI
- No GPU/RDMA path tested in CI
- Driver correctness is validated only by manual hardware testing

## Coverage Gaps

**No automated kernel-level tests:**
The driver has no KUnit tests, no kselftest integration, and no mock-hardware test shims. All correctness verification requires actual FPGA/PCIe hardware.

**No regression tests for ioctl interface:**
The userspace ioctl API (`DmaDriver.h` command codes) is not covered by any automated test. Breaking changes would only surface during manual testing.

**No concurrency stress tests in CI:**
Multi-descriptor opens, simultaneous read/write from multiple processes, and mask collision scenarios are only exercised by `dmaLoopTest` which requires hardware.

**No error-injection tests:**
Buffer exhaustion, IRQ storms, DMA errors, and driver removal under load are not tested in any automated way.

**No 32-bit addressing path test:**
The `is32` field in `DmaWriteData` and corresponding pointer truncation in `Dma_Read` has no automated test coverage.

**GPU async path:**
`gpu_async.c` and the `rdmaTest.cu` application are only compiled when `NVIDIA_DRIVERS` is set, and CI does not exercise this path at all.

**RCE stream / rce_memmap / rce_hp_buffers:**
These subdrivers (`rce_stream/`, `rce_memmap/`, `rce_hp_buffers/`) have no app-level tests and are not built or tested in CI.

---

*Testing analysis: 2026-03-26*
