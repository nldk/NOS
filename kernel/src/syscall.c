#include "syscall.h"
#include "utils.h"
#include "vga.h"
#include "process.h"
#include "storage.h"
#include "mem.h"

#define MAX_USER_PATH 256

static int copy_user_string(const char *user, char *out, uint64_t out_size) {
    if (!user || !out || out_size == 0) {
        return 0;
    }

    for (uint64_t i = 0; i < out_size; i++) {
        char c = user[i];
        out[i] = c;
        if (c == '\0') {
            return 1;
        }
    }

    out[out_size - 1] = '\0';
    return 0;
}

uint64_t syscall_dispatch(syscall_regs *regs) {
    if (!regs) {
        return (uint64_t)-1;
    }

    switch (regs->rax) {
        case SYS_EXIT: { // SYS_EXIT
            return_to_kernel();
        }
        case SYS_PRINT_CHAR: { // SYS_PRINT_CHAR
            char c = (char)regs->rdi;
            char tmp[2];
            tmp[0] = c;
            tmp[1] = '\0';
            printf(tmp);
            return 1;
        }
        case SYS_PRINT_STR: { // SYS_PRINT_STR
            const char *str = (const char *)regs->rdi;
            uint64_t len = regs->rsi;
            const uint64_t max_len = 4096;
            uint64_t copied = 0;

            if (!str || len == 0) {
                return 0;
            }

            if (len > max_len) {
                len = max_len;
            }

            char kbuf[4097];
            for (uint64_t i = 0; i < len; i++) {
                char c = str[i];
                if (c == '\0') {
                    break;
                }
                kbuf[i] = c;
                copied++;
            }
            kbuf[copied] = '\0';
            printf(kbuf);
            return copied;
        }
        case SYS_READ_FILE: { // SYS_READ_FILE
            const char *user_path = (const char *)regs->rdi;
            char *user_buf = (char *)regs->rsi;
            uint64_t buf_size = regs->rdx;
            unsigned int file_size = 0;
            char kpath[MAX_USER_PATH];

            if (!copy_user_string(user_path, kpath, sizeof(kpath))) {
                return (uint64_t)-1;
            }

            if (!ext2_read_file(kpath, 0, 0, &file_size)) {
                return (uint64_t)-1;
            }

            if (user_buf && buf_size > 0) {
                unsigned int to_read = (unsigned int)buf_size;
                if (to_read > file_size) {
                    to_read = file_size;
                }

                unsigned char *kbuf = (unsigned char *)malloc(to_read);
                if (!kbuf) {
                    return (uint64_t)-1;
                }

                if (!ext2_read_file(kpath, kbuf, to_read, 0)) {
                    free(kbuf);
                    return (uint64_t)-1;
                }

                memcpy(user_buf, kbuf, to_read);
                free(kbuf);
            }

            return (uint64_t)file_size;
        }
        case SYS_READ_DIR: { // SYS_READ_DIR
            const char *user_path = (const char *)regs->rdi;
            Ext2DirEntry *user_entries = (Ext2DirEntry *)regs->rsi;
            uint64_t buf_size = regs->rdx;
            unsigned int count = 0;
            char kpath[MAX_USER_PATH];
            Ext2DirEntry *kentries = 0;

            if (!copy_user_string(user_path, kpath, sizeof(kpath))) {
                return (uint64_t)-1;
            }

            if (!ext2_read_dir(kpath, &kentries, &count)) {
                return (uint64_t)-1;
            }

            uint64_t bytes = (uint64_t)count * sizeof(Ext2DirEntry);
            if (user_entries && buf_size >= bytes) {
                memcpy(user_entries, kentries, bytes);
            }

            free(kentries);
            return bytes;
        }
        default:
            return (uint64_t)-1;
    }
}
