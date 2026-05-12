#ifndef MEM_H
#define MEM_H

#include <stdint.h>
#define KernelStackSize 0x9000
#define DEFRAGFEQ 50
extern char kernelStack[KernelStackSize];

extern char heapInited;
extern int nextAmountOfFreesUntilDefrag;

#define HeapStart ((void*)0x100000)
#define HeapMax   ((void*)0x200000)
#define PAGE_SIZE 4096
#define MEMORY_START 0x200000
#define MEMORY_END   0x5000000

#define TOTAL_MEMORY (MEMORY_END - MEMORY_START)
#define TOTAL_PAGES  (TOTAL_MEMORY / PAGE_SIZE)
#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER     (1ULL << 2)

static uint8_t bitmap[TOTAL_PAGES / 8];

static void set_bit(uint64_t bit)
{
    bitmap[bit / 8] |= (1 << (bit % 8));
}

static void clear_bit(uint64_t bit)
{
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static int test_bit(uint64_t bit)
{
    return bitmap[bit / 8] & (1 << (bit % 8));
}

typedef struct{
    char used;
    unsigned int size;
}HeapHeader;

 

void free(void* ptr);
void* malloc(unsigned int bytes);
void coalesceHeap();
void initHeap();


void pmm_init();
void* alloc_page();
void free_page(void* page);
static inline void *alloc_page_zero();
static inline uint64_t *next_table(uint64_t *table, int idx, uint64_t flags);
void map_page(uint64_t *pml4, uint64_t va, uint64_t pa, uint64_t flags);
uint64_t *create_user_pml4();
void write_cr3(uint64_t val);
uint64_t read_cr3(void);

#endif