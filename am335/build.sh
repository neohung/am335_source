#
export PATH=$PATH:$(pwd)/../toolchain/gcc-linaro-arm-linux-gnueabihf-4.8-2014.04_linux/bin
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- iu8_defconfig
time make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -j4 uImage

