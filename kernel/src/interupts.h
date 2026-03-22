#ifndef INTERUPTS_H
#define INTERUPTS_H

#include "utils.h"
#define DOUBLE_FAULT_IST 1
#define PIC1_DATA 0x21

extern uint8_t df_stack[4096];
struct idt_entry {
    uint16_t offset_low;   // bits 0-15 of handler address
    uint16_t selector;     // code segment selector in GDT
    uint8_t ist;           // stack table (0 if not used)
    uint8_t type_attr;     // type and attributes (0x8E for interrupt gate)
    uint16_t offset_mid;   // bits 16-31 of handler address
    uint32_t offset_high;  // bits 32-63 of handler address
    uint32_t zero;         // reserved
} __attribute__((packed));
struct idt_ptr {
    uint16_t limit;  // size of IDT - 1
    uint64_t base;   // address of IDT
} __attribute__((packed));
extern struct idt_entry idt[256];
struct interrupt_frame {
    uint64_t rip, cs, rflags, rsp, ss;
};
struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));
extern struct tss tss;
struct __attribute__((packed)) gdt_entry {
    uint64_t value;
};

struct __attribute__((packed)) gdt_tss_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid1;
    uint8_t  type;
    uint8_t  limit_high;
    uint8_t  base_mid2;
    uint32_t base_high;
    uint32_t reserved;
};

struct __attribute__((packed)) gdtr {
    uint16_t limit;
    uint64_t base;
};
extern struct gdt_entry gdt[3];
extern struct gdt_tss_entry tss_entry;
void set_idt_entry(int vector, void (*handler)());
void load_idt();
void doubleFaultHander(struct interrupt_frame* frame, uint64_t error_code) __attribute__((interrupt));
void setDoubleFaultHander();
static inline void gdt_set_entry(int i, uint64_t value) {
    gdt[i].value = value;
}
void gdt_set_tss(struct gdt_tss_entry* entry, struct tss* tss_ptr);
void tss_init();
void gdt_load();
void tss_load();
void init_gdt_tss();
void init_keyint();
void setKeyApplicationBind(void(*callback)(char));
void set_keyboard_handler();
void pic_unmask_keyboard();
void pic_unmask_timer();
void pic_remap();
void set_timer_handler();

extern struct KeyApplicationBind* keyApplicationBind;
extern struct KeyApplicationBind* applicationBind;

struct KeyApplicationBind{
    void (*callback)(char);
    char key;
};

#endif