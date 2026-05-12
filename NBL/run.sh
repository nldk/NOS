#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_DIR="$SCRIPT_DIR/../kernel"
KERNEL_BUILD_DIR="$KERNEL_DIR/build"

cd "$SCRIPT_DIR"

nasm main.asm -f bin -o bootloader.bin
nasm stage2.asm -f bin -o stage2.bin
nasm header.asm -f bin -o header.bin
nasm kernel.asm -f bin -o kernel.bin
nasm header2.asm -f bin -o header2.bin
cmake -S "$KERNEL_DIR" -B "$KERNEL_BUILD_DIR"
cmake --build "$KERNEL_BUILD_DIR"

kernel_size=$(wc -c < "$KERNEL_BUILD_DIR/kernel.bin")
if [ "$kernel_size" -gt 35840 ]; then
	echo "kernel.bin is $kernel_size bytes; expected <= 35840 bytes"
	exit 1
fi

dd if=/dev/zero of="floppy.img" bs=512 count=2880
dd if="bootloader.bin" of="floppy.img" bs=512 seek=0 conv=notrunc
dd if=stage2.bin of=floppy.img bs=512 seek=1 conv=notrunc
dd if=header.bin of=floppy.img bs=512 seek=3 conv=notrunc
dd if=header2.bin of=floppy.img bs=512 seek=4 conv=notrunc
dd if=kernel.bin of=floppy.img bs=512 seek=5 conv=notrunc
dd if="$KERNEL_BUILD_DIR/kernel.bin" of=floppy.img bs=512 seek=6 conv=notrunc

if [ ! -f "hdd.img" ]; then
	dd if=/dev/zero of="hdd.img" bs=1M count=640
	if command -v mke2fs >/dev/null 2>&1; then
		mke2fs -t ext2 -b 4096 -I 256 -F "hdd.img" >/dev/null
		echo "Created ext2 filesystem at LBA 0 on hdd.img"
	else
		echo "mke2fs not found; install e2fsprogs to format hdd.img" >&2
		exit 1
	fi
fi

qemu-system-x86_64 \
	-boot a \
	-drive file=floppy.img,format=raw,if=floppy \
	-drive file=hdd.img,format=raw,if=ide,index=0 \
	-serial stdio