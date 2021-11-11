NAME  := axi_stream_dma
HOME  := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
GITV  := petalinux

SRCS := $(wildcard $HOME/*.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))

ccflags-y := -I$(HOME)
ccflags-y += -DDMA_IN_KERNEL=1 -DGITV=\"$(GITV)\"
ccflags-y += -Wformat=0 -Wno-int-to-pointer-cast
ccflags-y += -g -DDEBUG

$(NAME)-objs := dma_buffer.o dma_common.o
$(NAME)-objs += axis_gen1.o axis_gen2.o axistreamdma.o
obj-m        := $(NAME).o

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(HOME)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(HOME) modules_install

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers
