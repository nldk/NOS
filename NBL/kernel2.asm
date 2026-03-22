org 0x3000

bits 32

main:
    cli
    mov si, jmpInfo
    call printString
    hlt

cursor_row dd 0
cursor_col dd 0

printString:
    pusha
.loop:
    lodsb
    cmp al,0
    jz .end
    call printChar32
    jmp .loop
.end:
    popa
    ret

printChar32:
    ; input: AL = ASCII char
    mov eax, [cursor_row]
    mov ebx, [cursor_col]
    mov ecx, 80
    imul ecx         ; EAX = row * 80
    add eax, ebx     ; EAX = row*80 + col
    shl eax, 1       ; multiply by 2 → byte offset       ; multiply by 2 (2 bytes per cell)
    mov edi, 0xB8000
    add edi, eax
    mov [edi], al     ; ASCII character
    mov byte [edi+1], 0x07 ; attribute: light gray on black

    ; update cursor
    inc dword [cursor_col]
    cmp dword [cursor_col], 80
    jb .done
    mov dword [cursor_col], 0   ; reset column
    inc dword [cursor_row]

.done:
    ret
jmpInfo: db "printing from kernel 2",0
times 512-($-$$) db 0