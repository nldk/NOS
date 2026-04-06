#ifndef MEM_H
#define MEM_H

#define DEFRAGFEQ 50
extern char heapInited;
extern int nextAmountOfFreesUntilDefrag;

#define HeapStart ((void*)0x001000)
#define HeapMax   ((void*)0x002000)

typedef struct{
    char used;
    unsigned int size;
}HeapHeader;

 

void free(void* ptr);
void* malloc(unsigned int bytes);
void coalesceHeap();
void initHeap();
#endif