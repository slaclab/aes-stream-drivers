<!--- ########################################################################################### -->

# How to compile axi_stream_driver with petalinux packager

```bash
# Create the module template
petalinux-create -t modules --name axistreamdma --enable 

# Remove the template
rm -rf project-spec/meta-user/recipes-modules/axistreamdma

# Copy the aes-stream-driver source code (-L flag to remove symbolic links)
cp -rfL /path/to/my/aes-stream-drivers/petalinux/axistreamdma project-spec/meta-user/recipes-modules/axistreamdma

# Add module to petalinux configurations
echo KERNEL_MODULE_AUTOLOAD = \"axi_stream_dma\" >> project-spec/meta-user/conf/petalinuxbsp.conf
echo IMAGE_INSTALL_append = \" axistreamdma\" >> build/conf/local.conf

# Build the module
petalinux-build -c axistreamdma

# Add module to project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi
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

# Rebuild the device-tree
petalinux-build -c device-tree -x cleansstate
petalinux-build -c device-tree

```

<!--- ########################################################################################### -->

# How to compile axi_memory_map with petalinux packager

```bash
# Create the module template
petalinux-create -t modules --name aximemorymap --enable 

# Remove the template
rm -rf project-spec/meta-user/recipes-modules/aximemorymap

# Copy the aes-stream-driver source code (-L flag to remove symbolic links)
cp -rfL /path/to/my/aes-stream-drivers/petalinux/aximemorymap project-spec/meta-user/recipes-modules/aximemorymap

# Add module to petalinux configurations
echo KERNEL_MODULE_AUTOLOAD = \"axi_memory_map\" >> project-spec/meta-user/conf/petalinuxbsp.conf
echo IMAGE_INSTALL_append = \" aximemorymap\" >> build/conf/local.conf

# Build the module
petalinux-build -c aximemorymap
```

<!--- ########################################################################################### -->
