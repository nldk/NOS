dw 6
dw 1
dw 0x2000
dw 1

db "kernel1",0
times 510-($-$$) db 0
dw 0xFAC0