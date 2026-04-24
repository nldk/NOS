***NOS(Niel Operating System)***

Nos containes a bootloader NBL, writen in asm and a kernel writen in C.

 **The bootloader**

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

**The kernel**

You can find the kerlen in `/kernel` and the sources in `/kernel/scr`. The kernel has several parts:

* vga
* memory utils and heap
* interupthandeling
* a file system*

*:work in progress
