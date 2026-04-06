#include "interupts.h"
#include "utils.h"
#include "vga.h"
#include "mem.h"

static const char scancode_to_ascii[128] = {
    0,    // 0x00
    27,   // 0x01 ESC
    '1',  // 0x02
    '2',  // 0x03
    '3',  // 0x04
    '4',  // 0x05
    '5',  // 0x06
    '6',  // 0x07
    '7',  // 0x08
    '8',  // 0x09
    '9',  // 0x0A
    '0',  // 0x0B
    '-',  // 0x0C
    '=',  // 0x0D
    '\b', // 0x0E Backspace
    '\t', // 0x0F Tab
    'q',  // 0x10
    'w',  // 0x11
    'e',  // 0x12
    'r',  // 0x13
    't',  // 0x14
    'y',  // 0x15
    'u',  // 0x16
    'i',  // 0x17
    'o',  // 0x18
    'p',  // 0x19
    '[',  // 0x1A
    ']',  // 0x1B
    '\n', // 0x1C Enter
    0,    // 0x1D Ctrl
    'a',  // 0x1E
    's',  // 0x1F
    'd',  // 0x20
    'f',  // 0x21
    'g',  // 0x22
    'h',  // 0x23
    'j',  // 0x24
    'k',  // 0x25
    'l',  // 0x26
    ';',  // 0x27
    '\'', // 0x28
    '`',  // 0x29
    0,    // 0x2A Left Shift
    '\\', // 0x2B
    'z',  // 0x2C
    'x',  // 0x2D
    'c',  // 0x2E
    'v',  // 0x2F
    'b',  // 0x30
    'n',  // 0x31
    'm',  // 0x32
    ',',  // 0x33
    '.',  // 0x34
    '/',  // 0x35
    0,    // 0x36 Right Shift
    '*',  // 0x37 Numpad *
    0,    // 0x38 Left Alt
    ' ',  // 0x39 Space
    0,    // 0x3A Caps Lock
    // Remaining entries can be 0
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

struct KeyApplicationBind* keyApplicationBind;
struct KeyApplicationBind* applicationBind;

// Global variable definitions
uint8_t __attribute__((aligned(16))) df_stack[4096];
struct idt_entry idt[256];
struct tss tss;
struct gdt_entry gdt[3];
struct gdt_tss_entry tss_entry;

struct {
    struct gdt_entry gdt[3];
    struct gdt_tss_entry tss;
} __attribute__((packed)) full_gdt;

void set_idt_entry(int vector, void (*handler)()) {
    uint64_t addr = (uint64_t)handler;
    idt[vector].offset_low  = addr & 0xFFFF;
    idt[vector].selector    = 0x08; // code segment in GDT
    idt[vector].ist         = 0;    // optional stack table
    idt[vector].type_attr   = 0x8E; // interrupt gate
    idt[vector].offset_mid  = (addr >> 16) & 0xFFFF;
    idt[vector].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vector].zero        = 0;
}

__attribute__((interrupt))
static void unhandled_irq_handler(struct interrupt_frame* frame) {
    (void)frame;
    outb(0x20, 0x20);
}

__attribute__((interrupt))
static void unhandled_exception_handler(struct interrupt_frame* frame) {
    (void)frame;
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

__attribute__((interrupt))
static void unhandled_exception_with_error_handler(struct interrupt_frame* frame, uint64_t error_code) {
    (void)frame;
    (void)error_code;
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

static void init_default_idt_handlers(void) {
    for (int i = 0; i < 256; i++) {
        set_idt_entry(i, unhandled_irq_handler);
    }

    for (int i = 0; i < 32; i++) {
        set_idt_entry(i, unhandled_exception_handler);
    }

    set_idt_entry(8, unhandled_exception_with_error_handler);
    set_idt_entry(10, unhandled_exception_with_error_handler);
    set_idt_entry(11, unhandled_exception_with_error_handler);
    set_idt_entry(12, unhandled_exception_with_error_handler);
    set_idt_entry(13, unhandled_exception_with_error_handler);
    set_idt_entry(14, unhandled_exception_with_error_handler);
    set_idt_entry(17, unhandled_exception_with_error_handler);
    set_idt_entry(21, unhandled_exception_with_error_handler);
    set_idt_entry(29, unhandled_exception_with_error_handler);
    set_idt_entry(30, unhandled_exception_with_error_handler);
}

void load_idt() {
    struct idt_ptr idtp;
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint64_t)&idt;
    __asm__ volatile("lidt %0" : : "m"(idtp));
}
__attribute__((interrupt))
void doubleFaultHander(struct interrupt_frame* frame, uint64_t error_code){
    (void)frame;
    (void)error_code;
    serial_write_string("Double Fault Occurred!\r\n");
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
void setDoubleFaultHander(){
    set_idt_entry(8, doubleFaultHander);
    idt[8].ist = DOUBLE_FAULT_IST;
}
void gdt_set_tss(struct gdt_tss_entry* entry, struct tss* tss_ptr) {
    uint64_t base = (uint64_t)tss_ptr;
    uint32_t limit = sizeof(struct tss) - 1;

    entry->limit_low = limit & 0xFFFF;
    entry->base_low = base & 0xFFFF;
    entry->base_mid1 = (base >> 16) & 0xFF;
    entry->type = 0x89; // present + TSS
    entry->limit_high = (limit >> 16) & 0x0F;
    entry->base_mid2 = (base >> 24) & 0xFF;
    entry->base_high = (base >> 32) & 0xFFFFFFFF;
    entry->reserved = 0;
}
void tss_init() {
    for (int i = 0; i < 7; i++)
        tss.ist[i] = 0;

    // Set IST1 (index 0) for double fault
    tss.ist[0] = (uint64_t)&df_stack[4096];

    tss.iopb_offset = sizeof(struct tss);
}
void gdt_load() {
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) gdtr;

    full_gdt.gdt[0].value = 0;                // null
    full_gdt.gdt[1].value = 0x00AF9A000000FFFF; // code
    full_gdt.gdt[2].value = 0x00AF92000000FFFF; // data

    gdt_set_tss(&full_gdt.tss, &tss);

    gdtr.limit = sizeof(full_gdt) - 1;
    gdtr.base  = (uint64_t)&full_gdt;

    __asm__ volatile ("lgdt %0" : : "m"(gdtr));

    // reload segments
    __asm__ volatile (
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "pushq $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        :
        :
        : "rax"
    );
}
void tss_load() {
    uint16_t tss_selector = 0x18; // after 3 GDT entries (3 * 8)

    __asm__ volatile ("ltr %0" : : "r"(tss_selector));
}
void init_gdt_tss() {
    init_default_idt_handlers();
    tss_init();
    gdt_load();
    tss_load();
    setDoubleFaultHander();
    load_idt();
}

void pic_remap() {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);

    outb(0x21, 0x20);
    outb(0xA1, 0x28);

    outb(0x21, 0x04);
    outb(0xA1, 0x02);

    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    // Start with all IRQs masked; unmask only vectors with installed handlers.
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

__attribute__((interrupt))
void timer_handler(struct interrupt_frame* frame) {
    (void)frame;
    outb(0x20, 0x20);
}

void set_timer_handler() {
    set_idt_entry(32, timer_handler);
}

void init_keyint(){
    applicationBind = malloc(sizeof(struct KeyApplicationBind));
    applicationBind->callback = 0;
    applicationBind->key = 0;
}

void setKeyApplicationBind(void(*callback)(char)){
    applicationBind->callback = callback;
}

__attribute__((interrupt))
void keyboard_handler(struct interrupt_frame* frame) {
    (void)frame;
    uint8_t scancode = inb(0x60); // Read scancode from keyboard controller
    // Ignore key-release scancodes and out-of-range values.
    if ((scancode & 0x80U) == 0U && scancode < sizeof(scancode_to_ascii)) {
        char ch = scancode_to_ascii[scancode];
        if (applicationBind && applicationBind->callback && ch != 0) {
            applicationBind->key = ch;
            applicationBind->callback(applicationBind->key);
        }
    }
    // Send End of Interrupt (EOI) to PIC
    outb(0x20, 0x20);
}


void pic_unmask_keyboard() {
    uint8_t mask = inb(PIC1_DATA); // read current mask
    mask &= ~(1 << 1);             // clear bit 1 (keyboard IRQ)
    outb(PIC1_DATA, mask);
}

void pic_unmask_timer() {
    uint8_t mask = inb(PIC1_DATA); // read current mask
    mask &= ~(1 << 0);             // clear bit 0 (timer IRQ)
    outb(PIC1_DATA, mask);
}

void set_keyboard_handler() {
    set_idt_entry(33, keyboard_handler);
}