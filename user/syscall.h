#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stdint.h>
#include "storage.h"
#define SYS_EXIT 1
#define SYS_PRINT_CHAR 2
#define SYS_PRINT_STR 3
#define SYS_READ_FILE 4
#define SYS_READ_DIR 5
#define SYS_GET_RECENT_INPUT 6
#define SYS_NEW_INPUT 7

static inline void sys_exit(uint64_t code) {
    __asm__ volatile(
        "mov %0, %%rdi\n"
        "mov $1, %%rax\n"
        "syscall\n"
        :
        : "r"(code)
        : "rax", "rdi", "rcx", "r11", "memory"
    );
    for (;;) { }
}

static inline void sys_print_char(char c) {
    __asm__ volatile(
        "mov %0, %%rdi\n"
        "mov $2, %%rax\n"
        "syscall\n"
        :
        : "r"((uint64_t)c)
        : "rax", "rdi", "rcx", "r11", "memory"
    );
}

static inline uint64_t sys_print_str(const char *s, uint64_t len) {
    uint64_t ret;
    __asm__ volatile(
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov $3, %%rax\n"
        "syscall\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(s), "r"(len)
        : "rax", "rdi", "rsi", "rcx", "r11", "memory"
    );
    return ret;
}
static inline uint64_t sys_read_file(const char *path, char *buf, uint64_t buf_size) {
    uint64_t ret;
    __asm__ volatile(
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $4, %%rax\n"
        "syscall\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"(buf), "r"(buf_size)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}
static inline uint64_t sys_read_dir(const char *path, void *entries, uint64_t buf_size) {
    uint64_t ret;
    __asm__ volatile(
        "mov %1, %%rdi\n"
        "mov %2, %%rsi\n"
        "mov %3, %%rdx\n"
        "mov $5, %%rax\n"
        "syscall\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"(entries), "r"(buf_size)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}
#endif