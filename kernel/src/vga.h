#ifndef VGA_H
#define VGA_H

typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

static volatile uint16_t *const VGA_BUFFER = (uint16_t *)0xB8000;
extern uint32_t cursor_row;
extern uint32_t cursor_col;

#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_SIZE (VGA_COLS * VGA_ROWS)

static void vga_scroll(void) {
    for (int i = 0; i < VGA_SIZE - VGA_COLS; i++) {
        VGA_BUFFER[i] = VGA_BUFFER[i + VGA_COLS];
    }
    for (int i = VGA_SIZE - VGA_COLS; i < VGA_SIZE; i++) {
        VGA_BUFFER[i] = (uint16_t)' ' | ((uint16_t)0x07 << 8);
    }
    cursor_row = VGA_ROWS - 1;
    cursor_col = 0;
}

static void print_char(char c) {
    if (c == '\n') {
        cursor_row++;
        cursor_col = 0;
        if (cursor_row >= VGA_ROWS) {
            vga_scroll();
        }
        return;
    }
    if (c == '\b'){
        if (cursor_col == 0) {
            if (cursor_row == 0) {
                return;
            }
            cursor_row--;
            cursor_col = VGA_COLS - 1;
        } else {
            cursor_col--;
        }

        uint32_t backspace_pos = cursor_row * VGA_COLS + cursor_col;
        VGA_BUFFER[backspace_pos] = (uint16_t)' ' | ((uint16_t)0x07 << 8);
        return;
    }
    uint32_t pos = cursor_row * VGA_COLS + cursor_col;
    if (pos >= VGA_SIZE) {
        vga_scroll();
        pos = cursor_row * VGA_COLS + cursor_col;
    }
    
    VGA_BUFFER[pos] = (uint16_t)c | ((uint16_t)0x07 << 8);
    
    cursor_col++;
    if (cursor_col >= VGA_COLS) {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= VGA_ROWS) {
            vga_scroll();
        }
    }
}

static void printf(const char *text) {
    while (*text != '\0') {
        print_char(*text++);
    }
}

static void error_print_char(char c){
    if (c == '\n') {
        cursor_row++;
        cursor_col = 0;
        if (cursor_row >= VGA_ROWS) {
            vga_scroll();
        }
        return;
    }
    if (c == '\b'){
        if (cursor_col == 0) {
            if (cursor_row == 0) {
                return;
            }
            cursor_row--;
            cursor_col = VGA_COLS - 1;
        } else {
            cursor_col--;
        }

        uint32_t backspace_pos = cursor_row * VGA_COLS + cursor_col;
        VGA_BUFFER[backspace_pos] = (uint16_t)' ' | ((uint16_t)0x04 << 8);
        return;
    }
    uint32_t pos = cursor_row * VGA_COLS + cursor_col;
    if (pos >= VGA_SIZE) {
        vga_scroll();
        pos = cursor_row * VGA_COLS + cursor_col;
    }
    
    VGA_BUFFER[pos] = (uint16_t)c | ((uint16_t)0x04 << 8);
    
    cursor_col++;
    if (cursor_col >= VGA_COLS) {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= VGA_ROWS) {
            vga_scroll();
        }
    }
}

static void error_printf(const char *text){
    while (*text != '\0') {
        error_print_char(*text++);
    }
}
static void print_char_color(char c,unsigned char color){
    if (c == '\n') {
        cursor_row++;
        cursor_col = 0;
        if (cursor_row >= VGA_ROWS) {
            vga_scroll();
        }
        return;
    }
    if (c == '\b'){
        if (cursor_col == 0) {
            if (cursor_row == 0) {
                return;
            }
            cursor_row--;
            cursor_col = VGA_COLS - 1;
        } else {
            cursor_col--;
        }

        uint32_t backspace_pos = cursor_row * VGA_COLS + cursor_col;
        VGA_BUFFER[backspace_pos] = (uint16_t)' ' | ((uint16_t)color << 8);
        return;
    }
    uint32_t pos = cursor_row * VGA_COLS + cursor_col;
    if (pos >= VGA_SIZE) {
        vga_scroll();
        pos = cursor_row * VGA_COLS + cursor_col;
    }
    
    VGA_BUFFER[pos] = (uint16_t)c | ((uint16_t)color << 8);
    
    cursor_col++;
    if (cursor_col >= VGA_COLS) {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= VGA_ROWS) {
            vga_scroll();
        }
    }
}
static void printf_color(const char* text,unsigned char color){
    while (*text != '\0') {
        print_char_color(*text++,color);
    }
}
static void clearScreen(){
    for (int i = 0; i < VGA_SIZE; i++)
    {
        VGA_BUFFER[i] = (uint16_t)' ' | ((uint16_t)0x07 << 8);
    }
    cursor_col = 0;
    cursor_row = 0;
}
#endif