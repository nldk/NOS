dw 7 ;sector to start reading at
dw 20 ;sectors to read
dw 0x3000 ;mem adress to load at
dw 64 ;64 = long mode

db "kernel_c",0 ;name
times 510-($-$$) db 0
dw 0xFAC0