# Testing Strategy

**Analysis Date:** 2026-03-25

## Test Framework

**Runner:** None detected - No formal test framework (pytest, Google Test, etc.)

**Assertion Library:** None - Manual assertions via `printf` and return codes

**Run Commands:**
- No automated test commands defined in Makefile
- Applications serve as functional tests

## Test File Organization

**Location:**
- Test applications co-located with source code
- `common/app/` - Test applications (e.g., `dmaRead.cpp`, `dmaWrite.cpp`, `dmaLoopTest.cpp`)
- `data_dev/app/src/` - Data device test applications (e.g., `test.cpp`, `dmaRate.cpp`)
- `rce_stream/app/`, `rce_memmap/` - RCE module tests

**Naming:**
- Test files named descriptively: `dmaRead.cpp`, `dmaWrite.cpp`, `dmaLoopTest.cpp`
- Some use `test.cpp` in subdirectories

**Structure:**
```
common/app/           # Core DMA test apps
├── dmaRead.cpp       # DMA read testing
├── dmaWrite.cpp      # DMA write testing
├── dmaSetDebug.cpp   # Debug level testing
└── dmaLoopTest.cpp   # Bidirectional loopback test

data_dev/app/src/     # Data device tests
├── test.cpp          # Basic functionality tests
└── dmaRate.cpp       # Rate testing with threads

rce_stream/app/       # RCE stream tests (Makefile-based)
rce_memmap/app/       # RCE memory map tests
```

## Test Structure

**Suite Organization:**
```cpp
// Example from common/app/dmaRead.cpp
int main(int argc, char **argv) {
   // Parse arguments
   argp_parse(&argp, argc, argv, 0, 0, &args);

   // Open device
   s = open(args.path, O_RDWR);

   // Setup buffers (malloc or dmaMapDma)

   // Run test loop
   do {
      // Perform operation (read/write)
      // Validate results
      // Update counters
   } while (count < args.count);

   // Cleanup
   close(s);
   return 0;
}
```

**Patterns:**
- **Setup:** `open()` device, allocate buffers
- **Execution:** Loop with performance measurements
- **Teardown:** `close()` device, free buffers
- **Assertions:** Manual checks via return values, PRBS validation
- **PRBS Validation:** `PrbsData` class processes data for integrity checks

## Mocking

**Framework:** None - Direct hardware interaction

**Patterns:**
- No mocking framework used
- Hardware abstraction via driver interfaces
- Test applications communicate directly with hardware via `/dev/datadev_*` devices

**What to Mock:**
- Not applicable - tests require actual hardware

**What NOT to Mock:**
- Not applicable - all tests are integration/e2e style

## Fixtures and Factories

**Test Data:**
```cpp
// PRBS test data generation
PrbsData prbs(32, 4, 1, 2, 6, 31);  // Configuration for generator
prbs.genData(data, size);           // Generate test pattern
prbs.processData(data, size);       // Verify received data
```

**Location:**
- Test data generated on-the-fly via PRBS (Pseudo-Random Bit Sequence)
- No static fixture files

## Coverage

**Requirements:** None enforced

**View Coverage:**
- No coverage tools integrated

## Test Types

**Unit Tests:**
- None detected - No isolated unit tests for individual functions

**Integration Tests:**
- **DMA Operations:** `dmaRead.cpp`, `dmaWrite.cpp`
- **Loopback Tests:** `dmaLoopTest.cpp` - Creates bidirectional read/write threads
- **Rate Testing:** `data_dev/app/src/dmaRate.cpp` - Multi-threaded performance testing
- **GPU Integration:** `data_dev/app/src/rdmaTest.cu` - CUDA-based GPU testing

**E2E Tests:**
- All applications serve as E2E tests
- Test entire driver stack from user space to hardware

## Common Patterns

**Async Testing:**
```cpp
// Polling with select() for readiness
fd_set fds;
struct timeval timeout;
FD_ZERO(&fds);
FD_SET(s, &fds);
timeout.tv_sec = 2;
timeout.tv_usec = 0;
ret = select(s+1, &fds, NULL, NULL, &timeout);
```

**Error Testing:**
```cpp
// Check return values
if ((s = open(args.path, O_RDWR)) < 0) {
   printf("Error opening %s\n", args.path);
   return 1;
}
if ((rxData = malloc(maxSize)) == NULL) {
   printf("Failed to allocate rxData!\n");
   return 0;
}
```

**Performance Testing:**
```cpp
// Time-based measurements
gettimeofday(&startTime, NULL);
// ... operations ...
gettimeofday(&endTime, NULL);
timersub(&endTime, &startTime, &diffTime);
rate = (float)count / duration;
```

---

*Testing analysis: 2026-03-25*
