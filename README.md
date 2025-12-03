# aes-stream-drivers

[DOE Code](https://www.osti.gov/doecode/biblio/8043)

Common repository for streaming kernel drivers (datadev, gpuDirect, Yocto, etc)

<!--- ########################################################################################### -->

#### common/

Contains shared kernel and application libraries

#### data\_dev/

Contains driver and application code for TID-AIR generic DAQ PCIe cards, optionally with GPUDirect RDMA support (for use with NVIDIA GPUs)

/etc/modprobe.d/datadev.conf

options datadev cfgTxCount=1024 cfgRxCount=1024 cfgSize=131072 cfgMode=1 cfgCont=1

#### include/

Contains top level application include files for all drivers

#### rce\_hp\_buffers/

Contains driver that allocates memory blocks for use in a pure firmware dma engine

#### rce\_stream/

Contains driver and application code for the RCE AXI stream DMA

#### Yocto/

Contains BitBake recipes for the aximemorymap and axistreamdma drivers.

<!--- ########################################################################################### -->

# How to build the data\_dev driver

```bash
# Go to the base directory
$ cd aes-stream-drivers

# Build the drivers
$ make driver

# Build the applications
$ make app
```

## How to load the data\_dev driver

```bash
# Go to the base directory
$ cd aes-stream-drivers

# Load the driver for the current kernel
$ sudo insmod install/$(uname -r)/datadev.ko
```

<!--- ########################################################################################### -->

# How to use the Yocto recipes

The Yocto recipes can be trivially included in your Yocto project via symlink.

```bash
$ ln -s $aes_stream_drivers/Yocto/recipes-kernel $myproject/sources/meta-user/recipes-kernel
```

Make sure to set the following variables in your local.conf:
```bash
# Substitute these values with your own desired settings
DMA_TX_BUFF_COUNT = 128
DMA_RX_BUFF_COUNT = 128
DMA_BUFF_SIZE     = 131072
```

For a practical example of how to integrate these recipes into a Yocto project, see [axi-soc-ultra-plus-core](https://github.com/slaclab/axi-soc-ultra-plus-core).

<!--- ########################################################################################### -->

# How to build the RCE drivers

```bash
# Go to the base directory
$ cd aes-stream-drivers

# Source the setup script (required for cross-compiling)
$ source /sdf/group/faders/tools/xilinx/2016.4/Vivado/2016.4/settings64.sh

# Build the drivers
$ make rce
```

<!--- ########################################################################################### -->
