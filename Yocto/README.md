<!--- ########################################################################################### -->

# How to compile axi_stream_driver with Yocto

```bash
# Step 1 - Create a new application layer
# Note: Only perform this step if you do not already have an application layer created 
bitbake-layers create-layer $proj_dir/sources/meta-myapplications
bitbake-layers add-layer    $proj_dir/sources/meta-myapplications

# Step 2 - Copy a kernel module from aes-stream-driver into the local layer  (-L flag to remove symbolic links)
# Note: -L flag to remove symbolic links
mkdir -p $proj_dir/sources/meta-myapplications/recipes-kernel
cp -rfL /path/to/my/aes-stream-drivers/Yocto/recipes-kernel/axistreamdma $proj_dir/sources/meta-myapplications/recipes-kernel/.

# Step 3 - Enable kernel module build in the layer
echo "MACHINE_ESSENTIAL_EXTRA_RRECOMMENDS += \"axistreamdma\"" >> $proj_dir/conf/layer.conf

# Step 4 (Optional) - Enable automatic loading of kernel module
echo "KERNEL_MODULE_AUTOLOAD += \"axistreamdma\"" >> $proj_dir/conf/layer.conf

# Step 5 - add module to device tree
# Note: example below assume module's base address assigned to 0xb0000000 in Xilinx IP core
/ {
	axi_stream_dma_0@b0000000 {
		compatible = "axi_stream_dma";
		reg = <0x0 0xb0000000 0x0 0x10000>;
		interrupts = <0x0 0x6c 0x4>;
		interrupt-parent = <0x4>;
		slac,acp = <0x0>;
	};
};

# Step 6 - Build the module
bitbake core-image-minimal
```

<!--- ########################################################################################### -->

# How to compile axi_memory_map with Yocto

```bash
# Step 1 - Create a new application layer
# Note: Only perform this step if you do not already have an application layer created 
bitbake-layers create-layer $proj_dir/sources/meta-myapplications
bitbake-layers add-layer    $proj_dir/sources/meta-myapplications

# Step 2 - Copy a kernel module from aes-stream-driver into the local layer  (-L flag to remove symbolic links)
# Note: -L flag to remove symbolic links
mkdir -p $proj_dir/sources/meta-myapplications/recipes-kernel
cp -rfL /path/to/my/aes-stream-drivers/Yocto/recipes-kernel/aximemorymap $proj_dir/sources/meta-myapplications/recipes-kernel/.

# Step 3 - Enable kernel module build in the layer
echo "MACHINE_ESSENTIAL_EXTRA_RRECOMMENDS += \"aximemorymap\"" >> $proj_dir/conf/layer.conf

# Step 4 (Optional) - Enable automatic loading of kernel module
echo "KERNEL_MODULE_AUTOLOAD += \"aximemorymap\"" >> $proj_dir/conf/layer.conf

# Step 5 - No action required for device tree
# Note: axi_memory_map does NOT require an entire for the device-tree

# Step 6 - Build the module
bitbake core-image-minimal
```

<!--- ########################################################################################### -->
