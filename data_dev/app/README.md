# How to run the example application rate test

Go to the `app` directory and build the source code
```bash
$ cd aes-stream-drivers/data_dev/app
$ make
```

In one terminal, start the "dmaRate" application:
```bash
$ bin/dmaRate --count=100000
```

In another terminal, start the "dmaWrite" application:
```bash
$ bin/dmaWrite 0 --count=1000000
```

You should now start to see print outs in the 1st terminal:
```bash
$ bin/dmaRate --count=100000
  maxCnt           size      count   duration       rate         bw     Read uS   Return uS
       1      1.000e+03     100000   4.50e+00   2.22e+04   1.78e+08           2           1
       5      1.000e+03     100000   1.33e+00   7.51e+04   6.01e+08           3           1
       1      1.000e+03     100000   1.33e+00   7.54e+04   6.03e+08           2           0
       3      1.000e+03     100000   1.34e+00   7.44e+04   5.95e+08           3           1
       2      1.000e+03     100000   1.34e+00   7.47e+04   5.97e+08           3           1
       4      1.000e+03     100000   1.32e+00   7.56e+04   6.05e+08           3           1
       5      1.000e+03     100000   1.34e+00   7.45e+04   5.96e+08           3           1
       2      1.000e+03     100000   1.53e+00   6.53e+04   5.23e+08           3           1
       6      1.000e+03     100000   1.18e+00   8.46e+04   6.77e+08           3           0
```

Note: Do NOT turn on the debugging via (setDebug).  It will cause the interrupt handler to generate more interrupts and reduce performance

# How to run the rdmaTest application

`rdmaTest` performs GPUDirect RDMA I/O between an FPGA and the NVIDIA GPU installed on a system.

## Compiling rdmaTest

CUDA must be installed in /usr/local/cuda for the Makefile to work. If you have a different CUDA installation
path, set `NVCC=/path/to/your/cuda/bin/nvcc` when running `make`.
```bash
$ cd aes-stream-drivers/data_dev/app
$ make cuda
```

## Running

First, either configure your firmware to generate frames (via PrbsTx), or send frames using `dmaWrite`.

To run rdmaTest with a buffer size of 128k:
```bash
$ sudo ./bin/rdmaTest -s 0x20000
```

To run rdmaTest over 10, 128KB events, dumping the header of each:
```bash
$ sudo ./bin/rdmaTest -s 0x20000 -c 10 -vv
```

To send data from the GPU back to the FPGA, add `-r`. This will introduce some delay to the application,
but latency measurements are still accurate.
```bash
$ sudo ./bin/rdmaTest -s 0x20000 -r
```
