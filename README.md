***NOS(Niel Operating System)***

Nos containes a bootloader NBL, writen in asm and a kernel writen in C.
 **The bootloader**

As said is the bootloader writen in asm and can be campiled using nasm. It uses a header which is 1 sector big for discovering kernels(bootable programs) in a custom format that decleares a name a startaddress and the size in sectors. This is an example of a header. 
```asm
dw 6 #kernel starts at sector 6
dw 1 #kernel is 1 sector in size
dw 0x2000 #kerels entry-point is at address 0x2000
dw 1 #

db "kernel1",0 #the name to display for the options at boot
times 510-($-$$) db 0 
dw 0xFAC0 #magic number the bootloader looks for
```
