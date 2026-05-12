#include "interupts.h"
#include "utils.h"
#include "vga.h"
#include "mem.h"

#define KERNEL_CODE_SEG 0x08
#define KERNEL_DATA_SEG 0x10

#define USER_DATA_SEG   0x18
#define USER_CODE_SEG   0x20

#define TSS_SEG         0x28

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084

extern void syscall_entry(void);
uint64_t syscall_kernel_stack_top = 0;
uint64_t syscall_user_rsp = 0;

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

static inline void pic_io_wait(void) {
    outb(0x80, 0);
}

static void pic_send_eoi(int irq) {
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

struct KeyApplicationBind* keyApplicationBind;
struct KeyApplicationBind* applicationBind;

// Global variable definitions
uint8_t __attribute__((aligned(16))) df_stack[4096];
struct idt_entry idt[256];
struct tss tss;
struct gdt_entry gdt[5];
struct gdt_tss_entry tss_entry;

struct {
    struct gdt_entry gdt[5];
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
    pic_send_eoi(0);
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

static void serial_write_hex64(uint64_t value) {
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        unsigned int nibble = (unsigned int)((value >> ((15 - i) * 4)) & 0xF);
        buf[2 + i] = (nibble < 10) ? (char)('0' + nibble) : (char)('a' + (nibble - 10));
    }
    buf[18] = 0;
    serial_write_string(buf);
}

__attribute__((interrupt))
static void page_fault_handler(struct interrupt_frame* frame, uint64_t error_code) {
    uint64_t cr2 = 0;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    serial_write_string("Page fault\r\n");
    serial_write_string("cr2=");
    serial_write_hex64(cr2);
    serial_write_string(" rip=");
    serial_write_hex64(frame->rip);
    serial_write_string(" rsp=");
    serial_write_hex64(frame->rsp);
    serial_write_string(" err=");
    serial_write_hex64(error_code);
    serial_write_string("\r\n");

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
    set_idt_entry(14, page_fault_handler);
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

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

void syscall_init() {
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1ULL);

    syscall_kernel_stack_top = (uint64_t)kernelStack + KernelStackSize;

    // STAR[47:32] = kernel CS. STAR[63:48] = user CS - 16 for sysretq.
    uint64_t star = ((uint64_t)(USER_CODE_SEG - 0x10) << 48) |
                    ((uint64_t)KERNEL_CODE_SEG << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_SFMASK, 0x200ULL);
}
void tss_init() {
    for (int i = 0; i < 7; i++)
        tss.ist[i] = 0;

    // Set IST1 (index 0) for double fault
    tss.ist[0] = (uint64_t)&df_stack[4096];
    //tss.rsp0 = kernel_stack_top;
    tss.iopb_offset = sizeof(struct tss);
}
void gdt_load() {
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) gdtr;

    full_gdt.gdt[0].value = 0;
    full_gdt.gdt[1].value = 0x00AF9A000000FFFF;
    full_gdt.gdt[2].value = 0x00AF92000000FFFF;

    full_gdt.gdt[3].value = 0x00AFF2000000FFFF;
    full_gdt.gdt[4].value = 0x00AFFA000000FFFF;
    tss.rsp0 = (uint64_t)kernelStack + KernelStackSize;
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
    uint16_t tss_selector = TSS_SEG; // after 3 GDT entries (3 * 8)

    __asm__ volatile ("ltr %0" : : "r"(tss_selector));
}
void init_gdt_tss() {
    init_default_idt_handlers();
    tss_init();
    gdt_load();
    tss_load();
    syscall_init();
    setDoubleFaultHander();
    load_idt();
}

void pic_remap() {
    outb(0x20, 0x11);
    pic_io_wait();
    outb(0xA0, 0x11);
    pic_io_wait();

    outb(0x21, 0x20);
    pic_io_wait();
    outb(0xA1, 0x28);
    pic_io_wait();

    outb(0x21, 0x04);
    pic_io_wait();
    outb(0xA1, 0x02);
    pic_io_wait();

    outb(0x21, 0x01);
    pic_io_wait();
    outb(0xA1, 0x01);
    pic_io_wait();

    // Start with all IRQs masked; unmask only vectors with installed handlers.
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

__attribute__((interrupt))
void timer_handler(struct interrupt_frame* frame) {
    (void)frame;
    pic_send_eoi(0);
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
    pic_send_eoi(1);
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

void toRing3(uint64_t user_rip, uint64_t user_rsp){
    uint64_t rflags;
    uint64_t user_data = (uint64_t)(USER_DATA_SEG | 3);
    uint64_t user_code = (uint64_t)(USER_CODE_SEG | 3);

    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));
    rflags |= 0x200;

    __asm__ volatile (
        "mov %w0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushq %0\n"
        "pushq %1\n"
        "pushq %2\n"
        "pushq %3\n"
        "pushq %4\n"
        "iretq\n"
        :
        : "r"(user_data), "r"(user_rsp), "r"(rflags), "r"(user_code), "r"(user_rip)
        : "rax", "memory"
    );
}

void toRing3_cr3(uint64_t user_cr3, uint64_t user_rip, uint64_t user_rsp){
    uint64_t rflags;
    uint64_t user_data = (uint64_t)(USER_DATA_SEG | 3);
    uint64_t user_code = (uint64_t)(USER_CODE_SEG | 3);

    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));
    rflags |= 0x200;

    __asm__ volatile (
        "mov %0, %%cr3\n"
        "mov %w1, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushq %1\n"
        "pushq %2\n"
        "pushq %3\n"
        "pushq %4\n"
        "pushq %5\n"
        "iretq\n"
        :
        : "r"(user_cr3), "r"(user_data), "r"(user_rsp), "r"(rflags), "r"(user_code), "r"(user_rip)
        : "rax", "memory"
    );
}

