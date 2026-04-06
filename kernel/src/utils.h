#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

static void serial_write_char(char c) {
    for (unsigned int timeout = 0; timeout < 1000000U; timeout++) {
        if ((inb(0x3FD) & 0x20) != 0) {
            outb(0x3F8, (unsigned char)c);
            return;
        }
    }
}

static void serial_write_string(const char *text) {
    while (*text) {
        serial_write_char(*text++);
    }
}

void str_cp(char* str1, char* str2);
unsigned int str_len(char* str);
void cp_buff(unsigned char* src, unsigned char* dest, unsigned int size);
void trim(char* str);
char str_cmp(char* str1, char* str2);
unsigned int split(char** buff, unsigned int buffSize, char c, char* str);
void* memcpy(void* dest, const void* src, unsigned int n);
double ceil(double x);
double floor(double x);
#endif