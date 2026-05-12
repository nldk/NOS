.global syscall_entry
.extern syscall_dispatch
.extern syscall_kernel_stack_top
.extern syscall_user_rsp

.section .text
syscall_entry:
    mov %rsp, syscall_user_rsp(%rip)
    mov syscall_kernel_stack_top(%rip), %rsp

    pushq %rcx
    pushq %r11
    pushq %rax
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r10
    pushq %r8
    pushq %r9

    mov %rsp, %rdi
    call syscall_dispatch

    popq %r9
    popq %r8
    popq %r10
    popq %rdx
    popq %rsi
    popq %rdi
    addq $8, %rsp
    popq %r11
    popq %rcx

    mov syscall_user_rsp(%rip), %rsp
    sysretq
