PWD         := $(shell pwd) 
KVERSION    := $(shell uname -r)
KERNEL_DIR   = $(shell pwd)/../../

MODULE_NAME  = hello
obj-m       := $(MODULE_NAME).o   

all:
	make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERNEL_DIR) M=$(PWD) modules
clean:
	make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERNEL_DIR) M=$(PWD) clean 
