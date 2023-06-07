# K-Boot

K-Boot is a bootloader based on Linux kexec.

There is several benefits with this kind of bootloader solution:
- no need to duplicate/integrate drivers from Kernel
- no need to maintain another bootloader project
- same kernel source code used between Linux and K-Boot
- better boot/flashing performance
- easier to extend or debug (many tools available)
- use/contribute existing reliable projects

For now K-Boot target only `arm64` hardware.

### Setup environment

Fetch K-Boot project:
``` console
$ git clone --recurse-submodules git@github.com:baylibre/k-boot.git
```

Download toolchains and add to `$PATH`:
``` console
# aarch64-linux-musl toolchain for userspace binaries
$ wget https://musl.cc/aarch64-linux-musl-cross.tgz
$ tar zxvf aarch64-linux-musl-cross.tgz

# aarch64-none-linux toolchain for linux
$ wget https://developer.arm.com/-/media/Files/downloads/gnu-a/10.3-2021.07/binrel/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu.tar.xz
$ tar -xvf gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu.tar.xz
```

### Linux requirements

K-Boot doesn't manage linux project neither provide the defconfig.

However the user must set in the defconfig:
```
CONFIG_INITRAMFS_SOURCE="usr/initramfs/setup"
CONFIG_KEXEC=y
```

### Build K-Boot

``` console
$ ./build/build_all.sh --linux=~/src/linux --defconfig=kboot_defconfig
```

All others build scripts can be run independently (`--help` for more infos).

### Intregration with Arm Trusted Firmware (ATF)

In the ARM boot flow, K-Boot will be used as `BL33`.

According to [Booting AArch64 Linux](https://docs.kernel.org/arm64/booting.html):
> Primary CPU general-purpose register settings:
> - x0 = physical address of device tree blob (dtb) in system RAM.

Thus the device tree must be loaded before booting to K-Boot and `x0` must be set to the physical address where the device tree has been loaded.

This task can be fulfilled by the `BL2`.

ATF Boot Flow example with K-Boot:
1. MMC BOOT0 contains BL2 + Device Tree for K-Boot
2. BL1 (ROM code) copy MMC BOOT0 to SRAM
3. BL1 jump to SRAM and execute BL2
4. BL2 initialize DDR, extract from fip image: BL31 (pm runtime services), BL32 (OP-TEE) and BL33 (K-Boot)
5. BL2 copy the Device Tree for K-Boot found in SRAM to DDR and set x0 register
6. BL2 jump to BL31
7. BL31 boot and jump to OP-TEE
8. OP-TEE boot and jump to K-Boot
9. K-Boot kernel start, find init process in initramfs and execute K-Boot daemon (kbootd)

### Tips

Send `kbootd` over serial:
``` console
# On host
$ sx kbootd < /dev/ttyUSB0 > /dev/ttyUSB0

# On device, make sure kbootd is not running
# rx /bin/kbootd
```

### Contributions

`kbootd` coding style:
``` console
$ clang-format -i kbootd/src/*
```
