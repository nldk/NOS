CC      ?= gcc
LD      ?= ld
OBJCOPY ?= objcopy

CFLAGS  := -m64 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin -nostdlib -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mno-80387 -msoft-float -Wall -Wextra -Os
LDFLAGS := -m elf_x86_64 -T linker.ld -nostdlib

SRC := main.c
OBJ := $(SRC:.c=.o)

.PHONY: all clean

all: kernel.bin

kernel.elf: $(OBJ) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

kernel.raw: kernel.elf
	$(OBJCOPY) -O binary $< $@

kernel.bin: kernel.raw
	@size=$$(wc -c < kernel.raw); \
	if [ $$size -gt 1024 ]; then \
		echo "kernel.raw is $$size bytes; must be <= 1024"; \
		exit 1; \
	fi
	cp kernel.raw $@
	truncate -s 1024 $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) kernel.elf kernel.raw kernel.bin
