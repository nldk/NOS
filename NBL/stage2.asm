org 0x1000

bits 16

main:
    cli
    xor ax,ax
    mov ds, ax
    mov es, ax
    mov [bootDrive], dl
    call serialInit
    mov al, 'S'
    call serialPutChar
    mov si, ax
    mov di, ax
    mov bx, 0x9000
    mov ss, bx
    mov sp, 0xFFF9
    mov bx, ax
    mov cx, ax
    mov word [kernelCount], 0
    mov si, helloInfo
    call printString
    call scanDiskForKernels
    call printKernels
    call inputKernels
    hlt

printString:
.start:
    pusha
.loop:
    lodsb
    cmp al,0
    jz .end
    call printChar
    jmp .loop
.end:
    popa
    ret

printChar:
    mov ah, 0x0e
    int 0x10
    ret

serialInit:
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 0x03
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al
    ret

serialPutChar:
    push ax
    push dx
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, 0x3F8
    pop dx
    pop ax
    out dx, al
    ret

 ;set al to amount of sectors to be read, cl to the sector where to start and bx to the mem address to load it to
readDisk:
    pusha
    mov ah, 0x02        ; BIOS read sector
    mov ch, 0x00        ; cylinder 0
    mov dh, 0x00        ; head 0
    mov dl, [bootDrive] ; BIOS boot drive
    int 0x13
    jc diskError
    popa
    ret
diskError:
    mov si, diskErrorString
    call printString
    popa
    ret

print_hex:
    pusha
    mov bx, ax        ; copy AX so original value isn’t destroyed
    mov cx, 4         ; 4 hex digits

.hex_loop:
    rol bx, 4         ; rotate BX instead of AX
    mov al, bl
    and al, 0x0F      ; isolate nibble

    cmp al, 9
    jbe .digit
    add al, 'A' - 10
    jmp .print

.digit:
    add al, '0'

.print:
    mov ah, 0x0E      ; BIOS teletype
    int 0x10

    loop .hex_loop
    popa
    ret

scanDiskForKernels:
.start:
    pusha
    mov bx, 0x2000
    mov al, 1
    mov cl, 3

.readHeader:
    call readDisk
    mov ax, [bx + 510]
    cmp ax, 0xFAC0
    jnz .nextSector
    jmp .nextHeader

.nextSector:
    inc cl
    cmp cl, 18
    jz .end
    mov al, 1
    jmp .readHeader

.nextHeader:
    add bx, 512
    inc word [kernelCount]
    jmp .nextSector

.end:
    popa
    ret

printKernels:
.start:
    pusha
    mov bx,0x2000
    mov cx,[kernelCount]
    cmp cx, 0
    jz .end
.loop:
    mov si, newline
    mov ax, [bx]
    call print_hex
    call printString
    mov si, newline
    mov ax, [bx + 2]
    call print_hex
    call printString
    mov si, newline
    mov ax, [bx + 4]
    call print_hex
    call printString
    mov ax, [bx + 6]
    call print_hex
    call printString
    mov si, bx
    add si, 8
    call printString
    mov si, newline
    call printString
    dec cx
    cmp cx, 0
    jz .end
    add bx, 512
    jmp .loop
.end:
    popa
    ret

jumpToKernel:
.start:
    mov al, 'J'
    call serialPutChar
    mov bx, 0x2000
    mov ax, cx
    mov dx, 512
    mul dx
    add bx, ax
    mov di, bx
    mov cl, [bx]
    mov al, [bx+2]
    mov bx, [bx + 4]
    mov dx, [di + 6]
    mov [kernelMode], dx
    call readDisk
    mov al, 'R'
    call serialPutChar
    xor ax, ax
    push ax
    push bx
    cmp word [kernelMode], 1
    je .jump
    mov al, 'P'
    call serialPutChar
    mov ax, bx
    xor dx, dx
    mov [kernelAddr], ax
    call setCPUIn32_bitMode
    hlt
.jump:
    retf
inputKernels:
.start:
    call waitKey
    sub al, '0'
    cmp al, 1
    jb .start
    mov cl, [kernelCount]
    cmp al, cl
    ja .start
    xor cx, cx
    mov cl, al
    dec cl
    mov si, jumpInfo
    call printString
    call jumpToKernel
    hlt
waitKey:
    xor ah, ah      ; AH=0 for "read key" function
    int 0x16        ; BIOS keyboard interrupt     
    ret

gdt_start:
    dq 0                 ; null descriptor
    dq 0x00CF9A000000FFFF ; code segment: base=0, limit=4GB, 32-bit
    dq 0x00CF92000000FFFF ; data segment: base=0, limit=4GB, 32-bit
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

setCPUIn32_bitMode:
    cli
    mov dx, 0x3FD
.wait32a:
    in al, dx
    test al, 0x20
    jz .wait32a
    mov dx, 0x3F8
    mov al, '3'
    out dx, al
    in al, 0x92
    or al, 2
    out 0x92, al
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 08h:pm_start

bits 32           ; switch assembler to 32-bit mode
pm_start:
    cli
    mov ax, 10h     ; data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FFFC
    
    cmp word [0x1000 + kernelMode - 0x1000], 64
    jnz .jump_to_32bit_kernel
    jmp setCPUIn64_bitMode

.jump_to_32bit_kernel:
    mov eax, [0x1000 + kernelAddr - 0x1000]
    jmp eax

gdt64_start:
    dq 0
    dq 0x00AF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt64_end:

gdt64_descriptor:
    dw gdt64_end - gdt64_start - 1
    dd gdt64_start

setCPUIn64_bitMode:
    cli
    mov dx, 0x3FD
.wait32b:
    in al, dx
    test al, 0x20
    jz .wait32b
    mov dx, 0x3F8
    mov al, 'L'
    out dx, al
    
    ; Save kernel address in ESI (will persist across mode switch)
    mov esi, [0x1000 + kernelAddr - 0x1000]
    
    ; Load long-mode-capable GDT descriptor
    lgdt [gdt64_descriptor]

    ; Setup identity-mapped page tables at 0x5000-0x7000
    ; PML4 at 0x5000: single entry pointing to PDPT at 0x6000
    mov dword [0x5000], 0x6003
    mov dword [0x5004], 0x0
    
    ; PDPT at 0x6000: single entry pointing to PD at 0x7000
    mov dword [0x6000], 0x7003
    mov dword [0x6004], 0x0
    
    ; PD at 0x7000: identity map 0x0-2MB with large pages
    mov dword [0x7000], 0x83
    mov dword [0x7004], 0x0
    mov dword [0x7008], 0x200083
    mov dword [0x700C], 0x0

    ; Enable PAE (Physical Address Extension)
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax

    ; Load page table base address
    mov eax, 0x5000
    mov cr3, eax

    ; Enable long mode
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr

    ; Enable paging and protected mode
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax

    ; Far jump to 64-bit code
    jmp 0x8:longmode_start

bits 64
longmode_start:
    mov dx, 0x3FD
.wait64a:
    in al, dx
    test al, 0x20
    jz .wait64a
    mov dx, 0x3F8
    mov al, '6'
    out dx, al
    ; Clear segment registers and set up stack
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    mov rsp, 0x90000
    and rsp, -16
    sub rsp, 8
    xor rbp, rbp

    mov dx, 0x3FD
.wait64b:
    in al, dx
    test al, 0x20
    jz .wait64b
    mov dx, 0x3F8
    mov al, 'K'
    out dx, al
    
    ; Jump to kernel (address is in ESI from earlier)
    mov rax, rsi
    jmp rax
diskErrorString db "error reading disk",0x0D,0xA,0

helloInfo: db "stage 2 booted", 0x0D, 0xA,0
newline: db 0x0D, 0xA,0
jumpInfo db "jumping to kernel",0x0D, 0xA,0
kernelCount dw 0

kernelAddr: dd 0
kernelMode: dw 0
bootDrive: db 0
times 512 *2 -($-$$) db 0