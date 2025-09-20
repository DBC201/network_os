# network_os
A network os built on top of linux kernel. Tested and verified with raspberry pi 2b.

## External libraries used
- Clone [cpp_socket](https://github.com/DBC201/cpp_socket) under ```external/cpp_socket```
- Clone [cpp_utils](https://github.com/DBC201/cpp_utils) under ```external/cpp_utils```

## Building the Linux kernel

### Linux
- ```git clone git@github.com:torvalds/linux.git``` or use https
- Set environment variables. Ex. for raspberry pi: ```export ARCH=arm``` and ```export CROSS_COMPILE=arm-linux-gnueabihf-``` 
- Generate template config. ```make multi_v7_defconfig```
- Add the following to the genereated .config file:
```
#
# Networking etc.
#

# --- External initramfs ---
CONFIG_BLK_DEV_INITRD=y
CONFIG_RD_GZIP=y
# (do NOT set CONFIG_INITRAMFS_SOURCE when using external initramfs)

# --- Minimal FS your /init relies on ---
CONFIG_PROC_FS=y
CONFIG_SYSFS=y
CONFIG_DEVTMPFS=y
CONFIG_DEVTMPFS_MOUNT=y

# --- Exec formats ---
CONFIG_BINFMT_SCRIPT=y
CONFIG_BINFMT_ELF=y

# --- Keep built-in (no modules) for RAM-only system ---
CONFIG_MODULES=n

# --- Consoles (serial + HDMI text console) ---
CONFIG_SERIAL_AMBA_PL011=y
CONFIG_SERIAL_AMBA_PL011_CONSOLE=y
CONFIG_VT=y
CONFIG_VT_CONSOLE=y
CONFIG_FRAMEBUFFER_CONSOLE=y

# DRM console path (works with firmware/simplefb; VC4 = native Pi display)
CONFIG_DRM=y
CONFIG_DRM_KMS_HELPER=y
CONFIG_DRM_FBDEV_EMULATION=y
CONFIG_DRM_SIMPLEDRM=y
CONFIG_DRM_VC4=y

# --- Networking core (raw sockets) ---
CONFIG_NET=y
CONFIG_PACKET=y
CONFIG_UNIX=y
CONFIG_INET=y
CONFIG_IPV6=y
CONFIG_NETDEVICES=y

# --- Pi 2B USB host + onboard Ethernet and common USB NICs ---
CONFIG_USB=y
CONFIG_USB_DWC2=y
CONFIG_USB_DWC2_HOST=y
CONFIG_USB_USBNET=y
CONFIG_USB_NET_CDCETHER=y
CONFIG_USB_NET_RNDIS_HOST=y
CONFIG_USB_NET_SMSC95XX=y     # LAN9514 on-board USB+Ethernet hub
CONFIG_USB_RTL8152=y          # Realtek USB dongles

# --- Quality-of-life ---
CONFIG_TMPFS=y
CONFIG_HIGH_RES_TIMERS=y
CONFIG_KALLSYMS=y
CONFIG_PRINTK=y
```
- And finally build via ```make -j"$(nproc)" zImage dtbs```

## Building network_os
This program was developed in Ubuntu.
Install cmake if you don't have it by ```sudo apt install cmake``

- ```mkdir build && cd build```
- ```cmake ..```
- ```make .```
- See [this section](#booting-the-os) for how to use the build files on raspberry pi 2b.

## Booting the OS
### Raspberry Pi 2b
#### Custom OS Files
- Put ```/arch/arm/boot/zImage``` to the bootable drive
- Put ```/arch/arm/boot/dts/broadcom/bcm2836-rpi-2-b.dtb``` to the bootable drive
- Build this project in cmake and put the ```build/initramfs.cpio.gz``` to the bootable drive
#### Bootloader
- create cmdline.txt and paste this text: ```console=serial0,115200 console=tty1 rdinit=/init``` This text specifies to output console to hdmi.
- create config.txt and paste this text:
```
kernel=zImage
initramfs initramfs.cpio.gz followkernel
device_tree=bcm2836-rpi-2-b.dtb
enable_uart=1
```
This text specifies which kernel image along with device tree to load.
- To use the raspberry pi bootloader, download the [relevant raspberry pi image](https://www.raspberrypi.com/software/operating-systems/) (pi 2b is 32 bit). Copy the files in the bootfs partition so your final bootable looks like below:
```
ubuntu@actus-reus:/media/ubuntu/4A21-0000$ ls -l
total 40544
-rw-r--r-- 1 ubuntu ubuntu    20858 Sep 15 04:17 bcm2836-rpi-2-b.dtb
-rw-r--r-- 1 ubuntu ubuntu    52476 May 12 20:06 bootcode.bin
-rw-r--r-- 1 ubuntu ubuntu       50 Sep 15 05:40 cmdline.txt
-rw-r--r-- 1 ubuntu ubuntu      102 Sep 15 05:39 config.txt
-rw-r--r-- 1 ubuntu ubuntu     3230 May 12 20:06 fixup4cd.dat
-rw-r--r-- 1 ubuntu ubuntu     5456 May 12 20:06 fixup4.dat
-rw-r--r-- 1 ubuntu ubuntu     8449 May 12 20:06 fixup4db.dat
-rw-r--r-- 1 ubuntu ubuntu     8449 May 12 20:06 fixup4x.dat
-rw-r--r-- 1 ubuntu ubuntu     3230 May 12 20:06 fixup_cd.dat
-rw-r--r-- 1 ubuntu ubuntu     7325 May 12 20:06 fixup.dat
-rw-r--r-- 1 ubuntu ubuntu    10294 May 12 20:06 fixup_db.dat
-rw-r--r-- 1 ubuntu ubuntu    10290 May 12 20:06 fixup_x.dat
-rw-r--r-- 1 ubuntu ubuntu   677035 Sep 15 08:33 initramfs.cpio.gz
drwxr-xr-x 2 ubuntu ubuntu    32768 May 12 20:06 overlays
-rw-r--r-- 1 ubuntu ubuntu   814140 May 12 20:06 start4cd.elf
-rw-r--r-- 1 ubuntu ubuntu  3762408 May 12 20:06 start4db.elf
-rw-r--r-- 1 ubuntu ubuntu  2263968 May 12 20:06 start4.elf
-rw-r--r-- 1 ubuntu ubuntu  3011592 May 12 20:06 start4x.elf
-rw-r--r-- 1 ubuntu ubuntu   814140 May 12 20:06 start_cd.elf
-rw-r--r-- 1 ubuntu ubuntu  4834408 May 12 20:06 start_db.elf
-rw-r--r-- 1 ubuntu ubuntu  2988128 May 12 20:06 start.elf
-rw-r--r-- 1 ubuntu ubuntu  3735336 May 12 20:06 start_x.elf
-rw-r--r-- 1 ubuntu ubuntu 18031104 Sep 15 04:26 zImage
```
