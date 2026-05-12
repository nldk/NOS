#ifndef PROCESS_H
#define PROCESS_H
#include <stdint.h>
#include "utils.h"

#define MAX_PROCESSES 5
typedef struct {
    uint64_t rax;  // Accumulator register
    uint64_t rbx;  // Base register
    uint64_t rcx;  // Counter register
    uint64_t rdx;  // Data register
    uint64_t rsi;  // Source index register
    uint64_t rdi;  // Destination index register
    uint64_t rbp;  // Base pointer
    uint64_t rsp;  // Stack pointer
    uint64_t rip;  // Instruction pointer
    uint64_t r8;   // General-purpose register
    uint64_t r9;   // General-purpose register
    uint64_t r10;  // General-purpose register
    uint64_t r11;  // General-purpose register
    uint64_t r12;  // General-purpose register
    uint64_t r13;  // General-purpose register
    uint64_t r14;  // General-purpose register
    uint64_t r15;  // General-purpose register
    uint64_t eflags; // Flags register
    uint64_t cs;
    uint64_t ss;
} processRegs;

typedef struct {
    char name[256];
    unsigned int pid;
    uint64_t* page_table;
    void* entry;
    uint64_t stack_top;
    processRegs regs;
    bool active;
} Process;

static Process* processes[MAX_PROCESSES];
static Process* current_process;

void createUserProcessFromDiskFB(char* path);
void createUserStack(void* user_pml4, uint64_t user_stack_top);
void return_to_kernel(void) __attribute__((noreturn));
void* createUserProcess(char* name, void* entry, char* data, unsigned int size,void* userstackAddr);
void startProcess(Process* proc);
void pause_process(Process* proc, processRegs* regs);
void resume_process(Process* proc);

#endif