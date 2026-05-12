#include "vga.h"
#include "storage.h"
#include "utils.h"
#include "mem.h"
#include "interupts.h"
#include "process.h"
#include "syscall.h"

uint32_t cursor_row = 0;
uint32_t cursor_col = 0;

char buffer[512];
volatile int buffIndex = 0;
static char cwd[256] = "/";

static void reset_input_buffer(void){
    for (int i = 0; i < (int)sizeof(buffer); i++) {
        buffer[i] = 0;
    }
    buffIndex = 0;
}

void inputHandler(char c){
    if (c == '\b') {
        if (buffIndex > 0) {
            buffIndex--;
            buffer[buffIndex] = 0;
            print_char('\b');
        }
        return;
    }

    if (buffIndex < (int)sizeof(buffer) - 1){
        buffer[buffIndex++] = c;
        buffer[buffIndex] = 0;
        print_char(c);
    } else {
        error_printf("shellbuffer full");
    }
}

static int normalize_path(const char* input, char* out, unsigned int out_size) {
    char temp[512];
    unsigned int temp_len = 0;
    unsigned int out_len = 0;

    if (!input || !out || out_size < 2) {
        return 0;
    }

    if (input[0] == '/') {
        temp[0] = '/';
        temp_len = 1;
    } else {
        unsigned int cwd_len = str_len(cwd);
        if (cwd_len == 0) {
            cwd_len = 1;
            cwd[0] = '/';
            cwd[1] = 0;
        }
        if (cwd_len + 1 >= sizeof(temp)) {
            return 0;
        }
        memcpy(temp, cwd, cwd_len);
        temp_len = cwd_len;
        if (temp[temp_len - 1] != '/') {
            temp[temp_len++] = '/';
        }
    }

    while (*input) {
        if (temp_len + 1 >= sizeof(temp)) {
            return 0;
        }
        temp[temp_len++] = *input++;
    }
    temp[temp_len] = 0;

    out[0] = '/';
    out[1] = 0;
    out_len = 1;

    unsigned int i = 1;
    while (i < temp_len) {
        while (i < temp_len && temp[i] == '/') {
            i++;
        }
        if (i >= temp_len) {
            break;
        }

        unsigned int start = i;
        while (i < temp_len && temp[i] != '/') {
            i++;
        }
        unsigned int seg_len = i - start;

        if (seg_len == 1 && temp[start] == '.') {
            continue;
        }

        if (seg_len == 2 && temp[start] == '.' && temp[start + 1] == '.') {
            if (out_len > 1) {
                out_len--;
                while (out_len > 1 && out[out_len - 1] != '/') {
                    out_len--;
                }
                out[out_len] = 0;
            }
            continue;
        }

        if (out_len + seg_len + 1 >= out_size) {
            return 0;
        }
        if (out_len > 1 && out[out_len - 1] != '/') {
            out[out_len++] = '/';
        }
        for (unsigned int j = 0; j < seg_len; j++) {
            out[out_len++] = temp[start + j];
        }
        out[out_len] = 0;
    }

    return 1;
}

static unsigned int split_inplace(char** out, unsigned int out_size, char delim, char* str) {
    unsigned int count = 0;

    if (!out || !str || out_size == 0) {
        return 0;
    }

    while (*str && count < out_size) {
        while (*str == delim) {
            str++;
        }
        if (!*str) {
            break;
        }
        out[count++] = str;
        while (*str && *str != delim) {
            str++;
        }
        if (*str == delim) {
            *str = 0;
            str++;
        }
    }

    return count;
}

static void serial_write_hex8(uint8_t value) {
    char buf[5];
    buf[0] = '0';
    buf[1] = 'x';
    unsigned int high = (unsigned int)((value >> 4) & 0xF);
    unsigned int low = (unsigned int)(value & 0xF);
    buf[2] = (high < 10) ? (char)('0' + high) : (char)('a' + (high - 10));
    buf[3] = (low < 10) ? (char)('0' + low) : (char)('a' + (low - 10));
    buf[4] = 0;
    serial_write_string(buf);
}

static void shell_dump_input(void) {
    serial_write_string("shell: input len=");
    char* len_str = int_to_str(buffIndex);
    if (len_str) {
        serial_write_string(len_str);
        free(len_str);
    }
    serial_write_string(" data=");
    unsigned int max = (buffIndex > 32) ? 32U : (unsigned int)buffIndex;
    for (unsigned int i = 0; i < max; i++) {
        serial_write_hex8((uint8_t)buffer[i]);
        serial_write_char(' ');
    }
    serial_write_string("\r\n");
}


void shell(){
    reset_input_buffer();
    printf("Welcome to the NOS shell!\n");
    printf("Type 'help' for a list of commands.\n");
    while (1){
        printf(">>>");
        while (buffIndex == 0 || buffer[buffIndex - 1] != '\n'){}
        trim(buffer);
        shell_dump_input();
        char* component[20];
        unsigned int amount = split_inplace(component, 20, ' ', buffer);
        if (amount == 0) {
            error_printf("Empty command\n");
            serial_write_string("shell: empty command buffIndex=");
            char* idx = int_to_str(buffIndex);
            if (idx) {
                serial_write_string(idx);
                free(idx);
            }
            serial_write_string(" first=");
            if (buffIndex > 0) {
                serial_write_hex8((uint8_t)buffer[0]);
            } else {
                serial_write_string("0x00");
            }
            serial_write_string("\r\n");
            reset_input_buffer();
            continue;
        }
        if (amount > 0 && str_cmp(component[0],"help")){
            printf("help\necho\nclear\nata\nmount <lba>\nls [path]\ncd <path>\npwd\ncat <path>\nwrite <path> <text>\ncreate <path> <text>\ntouch <path>\nmkdir <path>\nrm <path>\nheaptest\n");
        }else if (amount > 0 && str_cmp(component[0],"echo")){
            for (unsigned int i = 1;i < amount;i++){
                printf(component[i]);
                serial_write_string(component[i]);
                if (i + 1 < amount) {
                    print_char(' ');
                    serial_write_char(' ');
                }
            }
            print_char('\n');
        }
        else if(amount > 0 && str_cmp(component[0],"clear")){
            clearScreen();
        }
        else if(amount > 0 && str_cmp(component[0],"ls")){
            char path[256];
            const char* target = (amount > 1) ? component[1] : cwd;
            Ext2DirEntry* entries;
            unsigned int count;
            if (!normalize_path(target, path, sizeof(path))) {
                error_printf("Invalid path\n");
            } else if (ext2_read_dir(path, &entries, &count)) {
                for (unsigned int i = 0; i < count; i++) {
                    printf(entries[i].name);
                    print_char('\n');
                }
                free(entries);
            } else {
                error_printf("Failed to read directory ");
                error_printf(path);
                print_char('\n');
            }
        }else if(amount > 1 && str_cmp(component[0],"cd")){
            char path[256];
            Ext2DirEntry* entries;
            unsigned int count;
            if (!normalize_path(component[1], path, sizeof(path))) {
                error_printf("Invalid path\n");
            } else if (ext2_read_dir(path, &entries, &count)) {
                free(entries);
                str_cp(path, cwd);
            } else {
                error_printf("Failed to change directory to ");
                error_printf(path);
                print_char('\n');
            }
        }else if(amount > 0 && str_cmp(component[0],"pwd")){
            printf(cwd);
            print_char('\n');
        }else if(amount > 1 && str_cmp(component[0],"cat")){
            char path[256];
            unsigned int size = 0;
            if (!normalize_path(component[1], path, sizeof(path))) {
                error_printf("Invalid path\n");
            } else if (!ext2_read_file(path, 0, 0, &size)) {
                error_printf("Failed to read file ");
                error_printf(path);
                print_char('\n');
            } else if (size > 65536) {
                error_printf("File too large\n");
            } else {
                unsigned char* data = (unsigned char*)malloc(size + 1);
                if (!data) {
                    error_printf("Out of memory\n");
                } else if (!ext2_read_file(path, data, size, 0)) {
                    error_printf("Failed to read file ");
                    error_printf(path);
                    print_char('\n');
                    free(data);
                } else {
                    data[size] = 0;
                    printf((char*)data);
                    print_char('\n');
                    free(data);
                }
            }
        }else if(amount > 2 && str_cmp(component[0],"write")){
            char path[256];
            if (!normalize_path(component[1], path, sizeof(path))) {
                error_printf("Invalid path\n");
            } else {
                unsigned int total = 0;
                for (unsigned int i = 2; i < amount; i++) {
                    total += str_len(component[i]);
                    if (i + 1 < amount) {
                        total++;
                    }
                }
                unsigned char* data = (unsigned char*)malloc(total + 1);
                if (!data) {
                    error_printf("Out of memory\n");
                } else {
                    unsigned int pos = 0;
                    for (unsigned int i = 2; i < amount; i++) {
                        unsigned int len = str_len(component[i]);
                        memcpy(data + pos, component[i], len);
                        pos += len;
                        if (i + 1 < amount) {
                            data[pos++] = ' ';
                        }
                    }
                    data[pos] = 0;
                    if (!ext2_write_file_overwrite(path, data, pos)) {
                        error_printf("Failed to overwrite file, err=");
                        char* err = int_to_str(ext2_last_error());
                        if (err) {
                            error_printf(err);
                            free(err);
                        }
                        print_char('\n');
                    } else {
                        printf("File written successfully\n");
                    }
                    free(data);
                }
            }
        }else if(amount > 2 && str_cmp(component[0],"create")){
            char path[256];
            if (!normalize_path(component[1], path, sizeof(path))) {
                error_printf("Invalid path\n");
            } else {
                unsigned int total = 0;
                for (unsigned int i = 2; i < amount; i++) {
                    total += str_len(component[i]);
                    if (i + 1 < amount) {
                        total++;
                    }
                }
                unsigned char* data = (unsigned char*)malloc(total + 1);
                if (!data) {
                    error_printf("Out of memory\n");
                } else {
                    unsigned int pos = 0;
                    for (unsigned int i = 2; i < amount; i++) {
                        unsigned int len = str_len(component[i]);
                        memcpy(data + pos, component[i], len);
                        pos += len;
                        if (i + 1 < amount) {
                            data[pos++] = ' ';
                        }
                    }
                    data[pos] = 0;
                    if (!ext2_create_file(path, data, pos)) {
                        error_printf("Failed to create file, err=");
                        char* err = int_to_str(ext2_last_error());
                        if (err) {
                            error_printf(err);
                            free(err);
                        }
                        print_char('\n');
                    } else {
                        printf("File created successfully\n");
                    }
                    free(data);
                }
            }
        }else if(amount > 1 && str_cmp(component[0],"touch")){
            char path[256];
            if (!normalize_path(component[1], path, sizeof(path))) {
                error_printf("Invalid path\n");
            } else {
                if (!ext2_create_file(path, (const unsigned char*)"", 0)) {
                    error_printf("Failed to create file, err=");
                    char* err = int_to_str(ext2_last_error());
                    if (err) {
                        error_printf(err);
                        free(err);
                    }
                    print_char('\n');
                } else {
                    printf("File created successfully\n");
                }
            }
        }else if(amount > 1 && str_cmp(component[0],"mkdir")){
            char path[256];
            if (!normalize_path(component[1], path, sizeof(path))) {
                error_printf("Invalid path\n");
            } else {
                if (!ext2_mkdir(path)) {
                    error_printf("Failed to create directory, err=");
                    char* err = int_to_str(ext2_last_error());
                    if (err) {
                        error_printf(err);
                        free(err);
                    }
                    print_char('\n');
                } else {
                    printf("Directory created successfully\n");
                }
            }
        }else if(amount > 1 && str_cmp(component[0],"rm")){
            char path[256];
            int recursive = 0;
            const char* target = component[1];
            if (amount > 2 && str_cmp(component[1], "-r")) {
                recursive = 1;
                target = component[2];
            }

            if (!normalize_path(target, path, sizeof(path))) {
                error_printf("Invalid path\n");
            } else {
                int ok = recursive ? ext2_delete_recursive(path) : ext2_delete(path);
                if (!ok) {
                    error_printf("Failed to delete, err=");
                    char* err = int_to_str(ext2_last_error());
                    if (err) {
                        error_printf(err);
                        free(err);
                    }
                    print_char('\n');
                } else {
                    printf("Deleted successfully\n");
                }
            }
        }else if(amount > 0 && str_cmp(component[0],"ata")){
            if (ata_smoke_test()) {
                printf("ata ok\n");
            } else {
                error_printf("ata failed\n");
            }
        }else if (amount > 0 && str_cmp(component[0],"heaptest")){
            void* ptr1 = malloc(100);
            void* ptr2 = malloc(200);
            void* ptr3 = malloc(300);
            printf("Allocated 3 blocks: 100, 200, 300 bytes\n");
            free(ptr2);
            printf("Freed the 200 byte block\n");
            void* ptr4 = malloc(150);
            printf("Allocated a 150 byte block (should fit into the freed 200 byte block)\n");
            free(ptr1);
            free(ptr3);
            free(ptr4);
            printf("Freed all blocks\n");
        }else if (amount > 1 && str_cmp(component[0],"mount")){
            unsigned int lba = str_to_int(component[1]);
            int exitCode = ext2_mount(lba);
            char* code = int_to_str(exitCode);
            printf(code);
            free(code);
        }else if(amount > 1 && str_cmp(component[0],"run")){
            char path[256];
            if (!normalize_path(component[1], path, sizeof(path))) {
                error_printf("Invalid path\n");
            }else{
                createUserProcessFromDiskFB(path);
                /*
                printf("Process created from ");
                printf(path);
                print_char('\n');
                */
            }

        }else{
            printf("Unknown command: ");
            printf(component[0]);
            print_char('\n');
        }
        reset_input_buffer();
    }
}

void kmain(void) {

    serial_init();
    clearScreen();
    initHeap();
    init_keyint();
    setKeyApplicationBind(inputHandler);
    init_gdt_tss();
    pic_remap();
    set_timer_handler();
    set_keyboard_handler();
    load_idt();
    pic_unmask_timer();
    pic_unmask_keyboard();
    printf("GDT and TSS initialized.\n");

    serial_write_string("kmain: before ata_smoke_test\r\n");
    if (ata_smoke_test()) {
        printf("ATA smoke test: ok\n");
    } else {
        error_printf("ATA smoke test: failed\n");
    }
    serial_write_string("kmain: after ata_smoke_test\r\n");

    __asm__ volatile("sti");
    serial_write_string("kmain: interrupts enabled\r\n");
    printf("Kernel initialized.\n");
    shell();

    for (;;) __asm__ volatile("hlt");
}