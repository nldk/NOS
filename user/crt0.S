.global _start
.extern user_main

.section .text
_start:
    call user_main
    mov $1, %rax
    xor %rdi, %rdi
    syscall
