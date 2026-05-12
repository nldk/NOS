CC ?= x86_64-elf-gcc
LD ?= x86_64-elf-ld
OBJCOPY ?= objcopy

CFLAGS = -m64 -ffreestanding -fno-pic -fno-pie -fno-builtin -nostdlib -Wall -Wextra -O2 -fno-stack-protector -mno-red-zone
LDFLAGS = -T linker.ld -nostdlib -no-pie --build-id=none

all: hello.bin

hello.o: hello.c syscall.h
	$(CC) $(CFLAGS) -c hello.c -o hello.o

crt0.o: crt0.S
	$(CC) $(CFLAGS) -c crt0.S -o crt0.o

hello.elf: crt0.o hello.o
	$(LD) $(LDFLAGS) -o hello.elf crt0.o hello.o

hello.bin: hello.elf
	$(OBJCOPY) -O binary hello.elf hello.bin

clean:
	rm -f *.o *.elf *.bin
