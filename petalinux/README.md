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
