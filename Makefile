CC      ?= x86_64-elf-gcc
OBJCOPY ?= objcopy

CFLAGS  := -std=c11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -O2 -Wall -Wextra
LDFLAGS := -T linker.ld -nostdlib -no-pie -Wl,-z,max-page-size=0x1000

CFLAGS  += -Os -fno-asynchronous-unwind-tables -fno-unwind-tables
LDFLAGS += -Wl,--build-id=none

SRC := main.c
OBJ := $(SRC:.c=.o)

.PHONY: all clean append

all: kernel.bin

kernel.elf: $(OBJ) linker.ld
	$(CC) $(CFLAGS) $(OBJ) $(LDFLAGS) -o $@

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

append: kernel.bin
	@test -n "$(IMG)" || (echo "Usage: make append IMG=path/to/disk.img" && exit 1)
	cat kernel.bin >> "$(IMG)"

clean:
	rm -f $(OBJ) kernel.elf kernel.bin