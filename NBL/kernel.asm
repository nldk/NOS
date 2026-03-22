org 0x2000

bits 16

main:
    cli
    mov si, jmpInfo
    call printString
    hlt
printString:
.start:
    push ax
    push si
.loop:
    lodsb
    cmp al,0
    jz .end
    call printChar
    jmp .loop
.end:
    pop si
    pop ax
    ret

printChar:
    mov ah, 0x0e
    int 0x10
    ret


jmpInfo: db "printing from kernel 1",0
times 512-($-$$) db 0