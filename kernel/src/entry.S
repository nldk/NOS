.global _start
.extern kmain

.section .text.boot, "ax"
_start:
    cli
    mov $0x9F000, %rsp
    and $-16, %rsp
    call kmain

.hang:
    hlt
    jmp .hang
