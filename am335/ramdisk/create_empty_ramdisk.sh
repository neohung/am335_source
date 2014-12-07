sudo dd if=/dev/zero of=/dev/ram0 bs=1k count=32768
sudo mke2fs -vm0 /dev/ram0 32768
sudo tune2fs -c 0 /dev/ram0
sudo dd if=/dev/ram0 bs=1k count=32768 | gzip -v9 > ramdisk_empty.gz
