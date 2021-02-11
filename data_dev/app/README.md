# How to cross-compile the test applications

To cross-compile the test applications you need to define the `CROSS_COMPILE` variable when calling `make`.

For example, to cross-compile the applications for the SLAC buildroot `2019.08` version, for the `x86_64` architecture, you should call `make` this way:

```bash
$ make \
CROSS_COMPILE=/afs/slac/package/linuxRT/buildroot-2019.08/host/linux-x86_64/x86_64/usr/bin/x86_64-buildroot-linux-gnu-
```

On the other hand, if you do not want to cross-compile the applications, and build it for the host instead, you need to call `make` without defining any variable:

```bash
$ make
```
