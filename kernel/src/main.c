#include "vga.h"
#include "storage.h"
#include "utils.h"
#include "mem.h"
#include "interupts.h"

uint32_t cursor_row = 0;
uint32_t cursor_col = 0;

char buffer[512];
volatile int buffIndex = 0;

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

void shell(){
    reset_input_buffer();
    printf("Welcome to the NOS shell!\n");
    printf("Type 'help' for a list of commands.\n");
    while (1){
        printf(">>>");
        while (buffIndex == 0 || buffer[buffIndex - 1] != '\n'){}
        trim(buffer);
        char* component[20];
        unsigned int amount = split(component,20,' ',buffer);
        if (amount > 0 && str_cmp(component[0],"help")){
            printf("help\necho\n");
        }
        if (amount > 0 && str_cmp(component[0],"echo")){
            for (unsigned int i = 1;i < amount;i++){
                printf(component[i]);
                if (i + 1 < amount) {
                    print_char(' ');
                }
            }
            print_char('\n');
        }
        if(amount > 0 && str_cmp(component[0],"clear")){
            clearScreen();
        }
        if(amount > 0 && str_cmp(component[0],"ls")){
            list_files();
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
    __asm__ volatile("sti");
    printf("Kernel initialized.\n");
    shell();
    for (;;) __asm__ volatile("hlt");
}