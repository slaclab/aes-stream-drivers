# pgpcard

Driver and utilities for legacy SLAC PGP PCIe cards.

| Card | PCI ID |
|---|---|
| PGP Gen2 | `1a4a:2000` |
| PGP Gen3 | `1a4a:2020` |

Builds `pgpcard.ko`, which registers `/dev/pgpcard_N` and `/proc/pgpcard_N`.

`driver/src/dma_common.{c,h}` and `driver/src/dma_buffer.{c,h}` are symlinks into
`common/driver/`, so this driver shares the DMA and character device layer with
`datadev` and inherits its Linux kernel version guards. Nothing pgpcard-specific
carries a `LINUX_VERSION_CODE` conditional, and none is needed.

`include/PgpDriver.h` and `include/FpgaProm.h` are kept here rather than in the
top-level `include/`, and `app_lib/PciCardProm.*` and `app_lib/McsRead.*` here
rather than in `common/app_lib/`, so that retiring this driver later touches
nothing shared. `PciCardProm` was removed from the shared library deliberately
(commit `39fcb5c`, "too many flavors of PROMs in the world to support").

## Building

```bash
make -C driver        # pgpcard.ko
make -C app           # utilities into app/bin/
```

Or from the repository root, for every installed kernel:

```bash
make pgpcard
```

### Selecting a kernel

`KVER` chooses which kernel's headers to build against, in the environment or on
the command line:

```bash
make -C driver KVER=5.14.0-687.42.1.el9_8.x86_64
```

`KERNELDIR` defaults to `/lib/modules/$(KVER)/build` and can be overridden when
the headers live elsewhere.

### Cross-compiling

For the driver, define `ARCH` and `CROSS_COMPILE` and point `KERNELDIR` at the
target's kernel source tree:

```bash
make -C driver \
  ARCH=x86_64 \
  CROSS_COMPILE=/path/to/toolchain/bin/x86_64-buildroot-linux-gnu- \
  KERNELDIR=/path/to/kernel/source
```

For the applications, only `CROSS_COMPILE` is needed:

```bash
make -C app CROSS_COMPILE=/path/to/toolchain/bin/x86_64-buildroot-linux-gnu-
```

## Loading

```bash
sudo insmod driver/pgpcard.ko
```

Module parameters: `cfgTxCount`, `cfgRxCount`, `cfgSize`, `cfgMode`, `cfgCont`.
Run `modinfo driver/pgpcard.ko` for descriptions and defaults.

## Applications

Built into `app/bin/`:

| Application | Purpose |
|---|---|
| `pgpGetStatus` | Print card info, PCI status and per-lane state |
| `pgpLoopTest` | Loopback data test across lanes and VCs |
| `pgpRead` | Read frames from the card |
| `pgpWrite` | Write frames to the card |
| `pgpSetData` | Set the per-lane sideband data value |
| `pgpSetDebug` | Set the driver debug level |
| `pgpSetLoop` | Enable or disable lane loopback |
| `pgpPromLoad` | Program the FPGA PROM from an MCS file |
| `pgpPromVerify` | Verify the FPGA PROM against an MCS file |

## Test coverage

CI covers compilation across the same distro matrix `datadev` uses, plus module
load and unload where the built module matches the runner kernel. Verified
building with no warnings against kernels 5.14.0 (Rocky 9), 5.15.0, 6.8.0,
7.1.12 and 7.3.0-rc0.

**CI cannot exercise probe, remove, DMA or interrupts.** There is no PGP card in
the test VM, and `emulator/driver` has no PGP personality: it emulates only
`1a4a:2030` with an AXIS Gen2 register map. A green CI run means the driver
compiles and its `module_init` and `module_exit` work. It says nothing about the
hardware path.

Run `bash scripts/ci-local/run_cell.sh --container rockylinux:9 --load-test 0 --phase pgp`
for the Rocky 9 build cell locally.

---

# Hardware verification procedure

For the person with the PGP-GEN3 card. Everything below runs on **your** Rocky 9
server, against the real card, because the hardware path cannot be verified
anywhere else.

Target hardware for these steps, from your `lspci`:

```
af:00.0 Signal processing controller: SLAC National Accelerator Lab
        TID-AIR PGP-GEN3 PCIe - 8 Lane Plus EVR   [1a4a:2020]
        Region 0: Memory at ee600000 (32-bit, non-prefetchable) [size=4K]
        Interrupt: pin A routed to IRQ 11
```

Please send back the output of every step, whether or not it looks wrong. Step 5
is the one we most need to see.

## 0. Build

```bash
git clone -b pgpcard-Rocky9 https://github.com/slaclab/aes-stream-drivers.git
cd aes-stream-drivers
make -C pgpcard/driver
make -C pgpcard/app
```

Expected: both complete with no errors. `pgpcard/driver/pgpcard.ko` exists and
`pgpcard/app/bin/` holds nine binaries.

This is already verified against Rocky 9's kernel headers
(`5.14.0-687.42.1.el9_8`) in a container, so a failure here most likely means a
missing prerequisite:

```bash
sudo dnf install -y kernel-devel-$(uname -r) kernel-headers-$(uname -r) gcc make
```

If `kernel-devel` for your exact running kernel is unavailable, enable the
CodeReady Builder repo, or reboot into a kernel whose `kernel-devel` you can
install. Send us `uname -r` and the error if it persists.

## 1. Confirm the card is visible and unclaimed

```bash
lspci -nn -d 1a4a:
lspci -k -s af:00.0
```

Expected: the device is listed and no `Kernel driver in use:` line appears. If a
driver is already bound, stop and tell us which one.

## 2. Load the module

```bash
sudo insmod pgpcard/driver/pgpcard.ko
sudo dmesg | tail -30
```

Expected in dmesg, roughly:

```
pgpcard: Init
pgpcard 0000:af:00.0: Init: Found card. Version=0x<...>, Type=0x<...>
pgpcard 0000:af:00.0: Init: Setting rx continue flag=1
pgpcard 0000:af:00.0: Init: IRQ 11
```

The `Found card` line is the important one. It means `PgpCard_Probe` ran and
`PgpCardG3_Init` read the firmware version register over the BAR.

Failure modes worth reporting verbatim:

| Symptom | What it means |
|---|---|
| `Invalid module format` | built for a different kernel; send `uname -r` and `modinfo -F vermagic pgpcard/driver/pgpcard.ko` |
| No `Found card` line | probe did not run or bailed; send all of `dmesg` |
| `Init: Failed to allocate hardware info` | out of memory at probe |
| `Probe: pci_enable_device() = <n>` | the PCI device could not be enabled |
| `Probe: Failed to set 32-bit DMA mask` | IOMMU or platform refused a 32-bit mask; unexpected on this card |
| `Probe: Failed to map register space` | BAR0 could not be mapped, possibly claimed by something else |
| Anything with `BUG:`, `Oops`, `WARNING:` | send the whole trace, this is the most valuable thing you could report |

## 3. Confirm the character device and proc entry

```bash
ls -l /dev/pgpcard*
cat /proc/pgpcard_0
```

Expected: `/dev/pgpcard_0` exists with mode `0666`, and `/proc/pgpcard_0` prints
a status block covering card info, PCI status and per-lane state. That read
exercises `PgpCard_InfoShow`, `PgpCard_PciShow` and `PgpCard_LaneShow`. If the
file exists but reading it hangs or oopses, that is a significant finding.

The device may be named `/dev/pgpcard_0` rather than `/dev/pgpcard0`; either is
fine, just tell us which you see.

## 4. Read status through the application

```bash
sudo pgpcard/app/bin/pgpGetStatus /dev/pgpcard_0
```

Expected: serial number, firmware version, lane count and per-lane link state,
plausible for an 8-lane Gen3 card with EVR.

Sanity checks: the serial should not be all zeros or all `f`s, and the firmware
version should match the `Version=` value from step 2. A version reading
`0xffffffff` usually means the BAR is mapped but the FPGA is not responding,
which is a hardware or firmware problem rather than a driver one.

## 5. Unload the module (the step that matters most)

```bash
sudo rmmod pgpcard
sudo dmesg | tail -20
```

Expected:

```
pgpcard: Remove: Remove called.
pgpcard 0000:af:00.0: Clean: Destroying device class
pgpcard: Remove: Driver is unloaded.
```

**Why this step is the one we care about.** While pgpcard was out of the tree,
`struct hardware_functions` gained an `irqEnable` member, and
`common/driver/dma_common.c:423` calls it during teardown while checking only
that `hwFunc` is non-NULL, not the member itself:

```c
if (dev->hwFunc)
   dev->hwFunc->irqEnable(dev, 0);
```

Both PGP function tables left `irqEnable` unset, so this dereferenced NULL and
would have oopsed on every removal of a probed card. We added
`PgpCardG2_IrqEnable` and `PgpCardG3_IrqEnable` and wired both tables.

That path is only reachable with a real card bound, because it lives in the
remove path. No CI matrix can reach it. **Your `rmmod` is the only thing that
will ever confirm this fix.**

If step 5 produces a kernel oops, send the full trace. If it completes cleanly,
that is the result we are waiting for.

## 6. Reload once

```bash
sudo insmod pgpcard/driver/pgpcard.ko && sleep 2 && sudo rmmod pgpcard
sudo dmesg | tail -20
```

Expected: a clean second cycle with no new warnings. This catches state that
survives removal incorrectly.

## What to send back

1. `uname -r` and `cat /etc/redhat-release`
2. Output of each step above
3. The complete `dmesg` across the whole sequence:
   ```bash
   sudo dmesg -c > /dev/null      # clear, then run steps 2 through 6
   sudo dmesg > pgpcard-dmesg.txt
   ```
4. Anything that looked odd, even if the command appeared to succeed

## Already verified, so you do not need to

- Builds clean with no warnings against Rocky 9's `5.14.0-687.42.1.el9_8` kernel
  headers in a `rockylinux:9` container
- Builds clean against 5.15, 6.8, 7.1 and 7.3-rc kernels as well
- Both PCI IDs advertised: Gen2 `1a4a:2000` and Gen3 `1a4a:2020`
- All nine applications build
- Register maps fit BAR0: `struct PgpCardG3Reg` is 3084 bytes, your BAR is 4096
- No missing Linux kernel version guards

## Known limitations

- PROM programming (`pgpPromLoad`, `pgpPromVerify`) builds but is completely
  untested. Do not use it on a card you cannot recover.
- Gen2 (`1a4a:2000`) support is compiled and reviewed, but no Gen2 card was
  available to test.
- The driver uses legacy INTx, matching `Interrupt: pin A routed to IRQ 11` in
  your `lspci`. It does not attempt MSI or MSI-X.
