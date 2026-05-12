#include "syscall.h"

static uint64_t str_len(const char *s) {
    uint64_t len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}

int user_main(void) {
    const char *msg = "Hello from user space!\n";
    sys_print_str(msg, str_len(msg));
    {
    char buf[512];
    uint64_t file_bytes = sys_read_file("/hello.txt", buf, sizeof(buf) - 1);
    if (file_bytes != (uint64_t)-1) {
        if (file_bytes >= sizeof(buf)) {
            file_bytes = sizeof(buf) - 1;
        }
        buf[file_bytes] = '\0';
        sys_print_str(buf, file_bytes);
    } else {
        sys_print_str("Failed to read file\n", 21);
    }
    }   
    Ext2DirEntry buff2[20];
    uint64_t dir_bytes = sys_read_dir("/", buff2, sizeof(buff2));
    if (dir_bytes != (uint64_t)-1) {
        if (dir_bytes > sizeof(buff2)) {
            sys_print_str("Directory buffer too small\n", 29);
            sys_exit(1);
        }
        uint64_t count = dir_bytes / sizeof(Ext2DirEntry);
        sys_print_str("Directory entries:\n", 19);
        for (uint64_t i = 0; i < count; i++) {
            Ext2DirEntry *entry = &buff2[i];
            sys_print_str(entry->name, str_len(entry->name));
            sys_print_str("\n", 1);
        }
    } else {
        sys_print_str("Failed to read directory\n", 25);
    }
    sys_exit(0);
    return 0;
}
