# NOS(Niel Operating System)

Nos containes a bootloader NBL, writen in asm and a kernel writen in C.

 ## The bootloader

The bootloader is written in asm and can be compiled using nasm. It is a 2 stage bootloader. It uses a header which is 1 sector big for discovering kernels(bootable programs) in a custom format that decleares a name a startaddress and the size in sectors. This is an example of a header. 
```asm
dw 6 #kernel starts at sector 6
dw 1 #kernel is 1 sector in size
dw 0x2000 #kerels entry-point is at address 0x2000
dw 1 #boot in 16-bit mode, change this value to 32 for 32-bit mode and 64 for 64-bit mode

db "kernel1",0 #the name to display for the options at boot
times 510-($-$$) db 0 
dw 0xFAC0 #magic number the bootloader looks for
```
You can find the code for the bootloader in `/NBL`.

## The kernel

You can find the kerlen in `/kernel` and the sources in `/kernel/scr`. The kernel has several parts:

* vga
* memory utils and heap
* interupthandeling
* a file system
* ring 3
* a minimal shell
* syscalls

### The Vga utilities

The vga utilities can be found in `vga.h`. All the functions are static. It provides functions to printing to the vga textbuffer. Using `print_char(char c)` you can put a char to the screen. The functions will automaticaly scroll if there is no space left. You can use `printf(const char *text)` to print a c string to the screen. 
Newlines in the form of `\n` are supported. If you want to print an error use the function prefix `error_` so `printf(char* text)` becomes `error_printf(char* text)`. Use `vga_scroll()` to scroll the screen manually or `clearScreen()` to clear the screen. All the printfunctions aswel as the scroll and clear function return `void`.

### memory utilities

The kernel provides various memory utilities like the heap and buffercopying.

#### The heap

You can find the code in `mem.h` and `mem.c`. The start and end of the heap are defined using `#define HeapStart ((void*)0x001000)` for the start and `#define HeapMax   ((void*)0x002000)` for the end. The heap uses headers for knowing what is allocated and what is free. THe heap needs to be initialized once which will write a header to the begin of the heap. The struct for the header is:

```c
typedef struct{
    char used;
    unsigned int size;
}HeapHeader;
```
Used is used to know if the memory is available and size is the amount of bytes to the next header. Use the `void initHeap();` function to initialize the heap. `void free(void* ptr);` to free a block of memory. To allocate use `void* malloc(unsigned int bytes);` to allocate n byten on the heap. Note that only the first 4MB are paged in the bootloader and because the kernel doesnt change it jet, it can only use memory up to 4MB.

#### memcpy

Memcpy is just the same as in the c standard lib and can be found in `utils.h`. The function defenition is `void* memcpy(void* dest, const void* src, unsigned int n)` where n is the amount of bytes and it return a void* to dest.

### utilities
You can find all the following functions in `utils.h` and `utils.c`.

#### str_cmp
String compare just compares string and returns 1 if the srings are equal and 0 if not. The fuction defenition is `char str_cmp(char* str1, char* str2)`. 

#### split
Split splits a string by the delimiter and puts it in the provided buffer. It uses malloc to alloczte mem for the string so remember to free it. It also takes the buffersize and won't override data if this argument is correct. Split returns tha amount of parts that have been put into the buffer. The defenition is `unsigned int split(char** buff, unsigned int buffSize, char c, char* str)`.

