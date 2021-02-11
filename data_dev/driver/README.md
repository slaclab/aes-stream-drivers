# How to cross-compile the kernel driver

To cross-compile the kernel driver you need to define the `ARCH` and `CROSS_COMPILE` variables when calling `make`. Also, you need to point `KERNELDIR` to the location of the kernel sources.

For example, to cross-compile the driver for the SLAC buildroot `2019.08` version for the `x86_64` architecture, you should call `make` this way:

```bash
$ make \
ARCH=x86_64 \
CROSS_COMPILE=/afs/slac/package/linuxRT/buildroot-2019.08/host/linux-x86_64/x86_64/usr/bin/x86_64-buildroot-linux-gnu- \
KERNELDIR=/afs/slac/package/linuxRT/buildroot-2019.08/buildroot-2019.08-x86_64/output/build/linux-4.14.139
```

On the other hand, if you do not want to cross-compile the driver, and build it for the host instead, you need to call `make` without defining any variable:

```bash
$ make
```
