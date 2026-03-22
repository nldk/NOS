org 0x7c00

bits 16

main:
    cli
    xor ax,ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    mov [bootDrive], dl
    mov si, loadinfInfo
    call printString
    call readDisk
    mov si, jmpInfo
    call printString
    mov dl, [bootDrive]
    jmp 0x0000:0x1000
    jmp halt

printString:
    lodsb
    cmp al, 0
    jz endPrintString
    call printChar
    jmp printString
endPrintString:
    ret
printChar:
    mov ah, 0x0e
    int 0x10
    ret

readDisk:
    mov ah, 0x02        ; BIOS read sectors
    mov al, 0x02        ; read 2 sectors
    mov ch, 0x00        ; cylinder 0
    mov cl, 0x02        ; sector 2 (we assume stage2 is here)
    mov dh, 0x00        ; head 0
    mov dl, [bootDrive] ; BIOS boot drive
    mov bx, 0x1000      ; load address 0000:1000
    int 0x13
    jc diskError
    ret

halt:
    hlt
    jmp halt
diskError:
    mov si, diskErrorString
    call printString
    jmp halt
loadinfInfo db "loading second stage",0x0D, 0xA, 0
jmpInfo db "jumping to second stage",0x0D,0xA,0
diskErrorString db "error reading disk",0x0D,0xA,0
bootDrive db 0
times 510-($-$$) db 0	; Pad the bootloader to 510 bytes
dw 0xAA55		

