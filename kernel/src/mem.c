#include "mem.h"
#include "vga.h"

char heapInited = 0;
int nextAmountOfFreesUntilDefrag = DEFRAGFEQ;
HeapHeader* lastHeapHeader = HeapStart;

void initHeap(){
    HeapHeader* ptr = (HeapHeader*)HeapStart;

    ptr->used = 0;
    ptr->size = (unsigned int)((char*)HeapMax - (char*)HeapStart - sizeof(HeapHeader));

    lastHeapHeader = (HeapHeader*)((char*)HeapStart + sizeof(HeapHeader) + ptr->size);

    heapInited = 1;
}

void* malloc(unsigned int bytes){
    if (!heapInited)
    {
        error_printf("Heap not initialized!\n");
        return 0;
    }
    
    if (bytes == 0) {
        return 0;
    }

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

    header = lastHeapHeader;
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

    HeapHeader* header = (HeapHeader*)((char*)ptr - sizeof(HeapHeader));
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