#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYS_EXIT 1
#define SYS_PRINT_CHAR 2
#define SYS_PRINT_STR 3
#define SYS_READ_FILE 4
#define SYS_READ_DIR 5

typedef struct syscall_regs {
    uint64_t r9;
    uint64_t r8;
    uint64_t r10;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rax;
    uint64_t r11;
    uint64_t rcx;
} syscall_regs;

uint64_t syscall_dispatch(syscall_regs *regs);

#endif
