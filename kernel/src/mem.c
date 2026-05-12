#include "mem.h"
#include "vga.h"
#include <stdint.h>
#include "utils.h"




char heapInited = 0;
int nextAmountOfFreesUntilDefrag = DEFRAGFEQ;
HeapHeader* lastHeapHeader = HeapStart;

static int heap_check(const char* tag) {
    if (!heapInited) {
        return 0;
    }

    if ((char*)lastHeapHeader < (char*)HeapStart || (char*)lastHeapHeader > (char*)HeapMax) {
        error_printf("Heap corruption detected (last header) in ");
        error_printf(tag);
        error_printf("\n");
        return 0;
    }

    HeapHeader* header = (HeapHeader*)HeapStart;
    unsigned int iterations = 0;
    while ((char*)header < (char*)lastHeapHeader) {
        if (header->size == 0 || header->size > (unsigned int)((char*)HeapMax - (char*)HeapStart)) {
            error_printf("Heap corruption detected (size) in ");
            error_printf(tag);
            error_printf("\n");
            return 0;
        }
        if (header->used != 0 && header->used != 1) {
            error_printf("Heap corruption detected (used) in ");
            error_printf(tag);
            error_printf("\n");
            return 0;
        }
        char* next = (char*)header + sizeof(HeapHeader) + header->size;
        if (next <= (char*)header || next > (char*)HeapMax) {
            error_printf("Heap corruption detected (next) in ");
            error_printf(tag);
            error_printf("\n");
            return 0;
        }
        header = (HeapHeader*)next;
        if (++iterations > 100000U) {
            error_printf("Heap corruption detected (loop) in ");
            error_printf(tag);
            error_printf("\n");
            return 0;
        }
    }

    return 1;
}

static int heap_ptr_valid(void* ptr) {
    if (!ptr) {
        return 0;
    }
    char* cptr = (char*)ptr;
    return cptr >= ((char*)HeapStart + (int)sizeof(HeapHeader)) && cptr < (char*)HeapMax;
}

void initHeap(){
    HeapHeader* ptr = (HeapHeader*)HeapStart;

    ptr->used = 0;
    ptr->size = (unsigned int)((char*)HeapMax - (char*)HeapStart - sizeof(HeapHeader));

    lastHeapHeader = (HeapHeader*)((char*)HeapStart + sizeof(HeapHeader) + ptr->size);

    heapInited = 1;
    heap_check("init");
}

void* malloc(unsigned int bytes){
    if (!heapInited)
    {
        error_printf("Heap not initialized!\n");
        return 0;
    }

    if (!heap_check("malloc")) {
        return 0;
    }
    
    if (bytes == 0) {
        return 0;
    }

    // Align to 8 bytes to reduce fragmentation and keep headers aligned.
    bytes = (bytes + 7U) & ~7U;

    for (int pass = 0; pass < 2; pass++) {
        HeapHeader* header = (HeapHeader*)HeapStart;
        while ((char*)header < (char*)lastHeapHeader) {
            if (!header->used && header->size >= bytes) {
                unsigned int originalSize = header->size;
                if (originalSize >= bytes + sizeof(HeapHeader) + 1) {
                    HeapHeader* split = (HeapHeader*)((char*)header + sizeof(HeapHeader) + bytes);
                    split->used = 0;
                    split->size = originalSize - bytes - sizeof(HeapHeader);
                    header->size = bytes;
                }
                header->used = 1;
                return (void*)((char*)header + sizeof(HeapHeader));
            }
            header = (HeapHeader*)((char*)header + sizeof(HeapHeader) + header->size);
        }

        // Try to coalesce once if the first pass didn't find space.
        if (pass == 0) {
            coalesceHeap();
        }
    }

    HeapHeader* header = lastHeapHeader;
    char* next = (char*)header + sizeof(HeapHeader) + bytes;
    if (next + sizeof(HeapHeader) > (char*)HeapMax) {
        return 0;
    }

    header->used = 1;
    header->size = bytes;
    lastHeapHeader = (HeapHeader*)next;
    *lastHeapHeader = (HeapHeader){0,0};

    return (void*)((char*)header + sizeof(HeapHeader));
}

void free(void* ptr){
    if (!heapInited)
    {
        error_printf("Heap not initialized!\n");
        return;
    }
    
    if (!ptr) {
        return;
    }

    if (!heap_ptr_valid(ptr)) {
        error_printf("free: invalid pointer\n");
        return;
    }

    HeapHeader* header = (HeapHeader*)((char*)ptr - sizeof(HeapHeader));
    if ((char*)header < (char*)HeapStart || (char*)header >= (char*)HeapMax) {
        error_printf("free: invalid header\n");
        return;
    }
    if (header->used == 0) {
        error_printf("free: double free\n");
        return;
    }
    if (header->size == 0 || header->size > (unsigned int)((char*)HeapMax - (char*)HeapStart)) {
        error_printf("free: corrupt header\n");
        return;
    }

    header->used = 0;
    HeapHeader* next = (HeapHeader*)((char*)header + sizeof(HeapHeader) + header->size);
    while ((char*)next < (char*)lastHeapHeader && next->used == 0 && next->size > 0) {
        header->size += sizeof(HeapHeader) + next->size;
        next = (HeapHeader*)((char*)header + sizeof(HeapHeader) + header->size);
    }
    nextAmountOfFreesUntilDefrag--;
    if (!nextAmountOfFreesUntilDefrag){
        coalesceHeap();
    }
    
}
void coalesceHeap() {
    if (!heapInited)
    {
        error_printf("Heap not initialized!\n");
        return;
    }
    if (!heap_check("coalesce")) {
        return;
    }
    HeapHeader* header = (HeapHeader*)HeapStart;
    while ((char*)header < (char*)lastHeapHeader) {
        if (!header->used) {
            HeapHeader* next = (HeapHeader*)((char*)header + sizeof(HeapHeader) + header->size);
            while ((char*)next < (char*)lastHeapHeader && !next->used && next->size > 0) {
                header->size += sizeof(HeapHeader) + next->size;
                next = (HeapHeader*)((char*)header + sizeof(HeapHeader) + header->size);
            }
        }
        header = (HeapHeader*)((char*)header + sizeof(HeapHeader) + header->size);
    }
    nextAmountOfFreesUntilDefrag = DEFRAGFEQ;
}

#define KERNEL_STACK_SIZE 65536

uint8_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));

// place canary at the bottom of the stack region
uint64_t *stack_canary = (uint64_t*)kernel_stack;

void pmm_init()
{
    for (uint64_t i = 0; i < sizeof(bitmap); i++) {
        bitmap[i] = 0;
    }

    // Reserve first few pages for kernel structures
    for (uint64_t i = 0; i < 256; i++) {
        set_bit(i);
    }
}
void* alloc_page()
{
    for (uint64_t i = 0; i < TOTAL_PAGES; i++) {

        if (!test_bit(i)) {

            set_bit(i);

            uint64_t addr =
                MEMORY_START + (i * PAGE_SIZE);

            return (void*)addr;
        }
    }

    return 0;
}
void free_page(void* page)
{
    uint64_t addr = (uint64_t)page;

    if (addr < MEMORY_START || addr >= MEMORY_END)
        return;

    uint64_t index =
        (addr - MEMORY_START) / PAGE_SIZE;

    clear_bit(index);
}
char kernelStack[KernelStackSize] __attribute__((aligned(16)));

#define PTE_P  (1ULL << 0)
#define PTE_W  (1ULL << 1)
#define PTE_U  (1ULL << 2)

uint64_t read_cr3(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(val));
    return val;
}

void write_cr3(uint64_t val) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(val) : "memory");
}

static inline void *alloc_page_zero() {
    void *p = alloc_page();
    if (p) {
        memset(p, 0, PAGE_SIZE);
    }
    return p;
}

static inline uint64_t *next_table(uint64_t *table, int idx, uint64_t flags) {
    if (table[idx] & PTE_P) {
        // Ensure existing entries become user-accessible when requested.
        table[idx] |= (flags & (PTE_P | PTE_W | PTE_U));
        return (uint64_t *)(table[idx] & ~0xFFFULL);
    }

    uint64_t *new_tbl = (uint64_t *)alloc_page_zero();
    table[idx] = ((uint64_t)new_tbl) | flags;
    return (uint64_t *)(table[idx] & ~0xFFFULL);
}

void map_page(uint64_t *pml4, uint64_t va, uint64_t pa, uint64_t flags) {
    int pml4_i = (va >> 39) & 0x1FF;
    int pdpt_i = (va >> 30) & 0x1FF;
    int pd_i   = (va >> 21) & 0x1FF;
    int pt_i   = (va >> 12) & 0x1FF;

    uint64_t *pdpt = next_table(pml4, pml4_i, PTE_P | PTE_W | (flags & PTE_U));
    uint64_t *pd   = next_table(pdpt, pdpt_i, PTE_P | PTE_W | (flags & PTE_U));
    uint64_t *pt   = next_table(pd, pd_i, PTE_P | PTE_W | (flags & PTE_U));

    pt[pt_i] = (pa & ~0xFFFULL) | flags;
}
uint64_t *create_user_pml4() {
    uint64_t *new_pml4 = alloc_page_zero();
    if (!new_pml4) {
        return 0;
    }

    // Build fresh low mappings to avoid inheriting large-page entries.
    for (uint64_t va = 0; va < 0x400000; va += PAGE_SIZE) {
        map_page(new_pml4, va, va, PAGE_PRESENT | PAGE_WRITABLE);
    }

    return new_pml4;
}
