#include "process.h"
#include "mem.h"
#include "interupts.h"
#include "utils.h"
#include "storage.h"

static uint64_t kernel_return_rsp = 0;
static void *kernel_return_rip = 0;
static uint64_t kernel_cr3 = 0;



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

static void dump_mapping(uint64_t *pml4, uint64_t va) {
    int pml4_i = (va >> 39) & 0x1FF;
    int pdpt_i = (va >> 30) & 0x1FF;
    int pd_i   = (va >> 21) & 0x1FF;
    int pt_i   = (va >> 12) & 0x1FF;

    serial_write_string("map: va=");
    serial_write_hex64(va);
    serial_write_string(" pml4e=");
    serial_write_hex64(pml4[pml4_i]);
    serial_write_string("\r\n");

    if (!(pml4[pml4_i] & PAGE_PRESENT)) {
        return;
    }

    uint64_t *pdpt = (uint64_t *)(pml4[pml4_i] & ~0xFFFULL);
    serial_write_string("map: pdpte=");
    serial_write_hex64(pdpt[pdpt_i]);
    serial_write_string("\r\n");
    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) {
        return;
    }

    if (pdpt[pdpt_i] & (1ULL << 7)) {
        return;
    }

    uint64_t *pd = (uint64_t *)(pdpt[pdpt_i] & ~0xFFFULL);
    serial_write_string("map: pde=");
    serial_write_hex64(pd[pd_i]);
    serial_write_string("\r\n");
    if (!(pd[pd_i] & PAGE_PRESENT)) {
        return;
    }

    if (pd[pd_i] & (1ULL << 7)) {
        return;
    }

    uint64_t *pt = (uint64_t *)(pd[pd_i] & ~0xFFFULL);
    serial_write_string("map: pte=");
    serial_write_hex64(pt[pt_i]);
    serial_write_string("\r\n");
}

void return_to_kernel(void) {
    write_cr3(kernel_cr3);
    __asm__ volatile(
        "mov %0, %%rsp\n"
        "jmp *%1\n"
        :
        : "r"(kernel_return_rsp), "r"(kernel_return_rip)
        : "memory"
    );
    __builtin_unreachable();
}

void createUserProcessFromDiskFB(char* path){
    uint64_t user_va = 0x400000;
    unsigned int size = 0;
    if (!ext2_read_file(path, 0, 0, &size)) {
        serial_write_string("createUserProcess: ext2_read_file failed path=");
        serial_write_string(path ? path : "(null)");
        serial_write_string("\r\n");
        serial_write_string("createUserProcess: last error=");
        char* err_str = int_to_str(ext2_last_error());
        if (err_str) {
            serial_write_string(err_str);
            free(err_str);
        }
        serial_write_string("\r\n");
        return;
    }
    serial_write_string("Creating user process\n");
    serial_write_string("user size=");
    char* size_str = int_to_str((int)size);
    if (size_str) {
        serial_write_string(size_str);
        free(size_str);
    }
    serial_write_string("\r\n");
    if (size == 0) {
        serial_write_string("createUserProcess: zero-length user image\r\n");
        return;
    }
    unsigned char *buf = malloc(size);
    if (!buf) {
        serial_write_string("createUserProcess: out of memory\r\n");
        return;
    }
    if (!ext2_read_file(path, buf, size, 0)) {
        serial_write_string("createUserProcess: read failed\r\n");
        free(buf);
        return;
    }
    void* user_pml4 = createUserProcess(path, (void*)user_va, buf, size,(void*)0x800000);
    free(buf);
    serial_write_string("Switching to user mode\n");
    toRing3_cr3((uint64_t)user_pml4, user_va, 0x800000);


}

void createUserStack(void* user_pml4, uint64_t user_stack_top) {
    const uint64_t stack_pages = 8;
    for (uint64_t i = 0; i < stack_pages; i++) {
        void *stack_page = alloc_page();
        if (!stack_page) {
            serial_write_string("createUserStack: alloc_page failed\r\n");
            break;
        }
        map_page((uint64_t *)user_pml4,
             user_stack_top - ((i + 1) * PAGE_SIZE),
             (uint64_t)stack_page,
             PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
    }
}



void* createUserProcess(char* name, void* entry, char* data, unsigned int size,void* userstackAddr) {
    void* user_pml4 = create_user_pml4();
    if (!user_pml4) {
        serial_write_string("createUserProcess: no user pml4\r\n");
        free(data);
        return NULL;
    }
    unsigned int pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0) {
        serial_write_string("createUserProcess: no pages to map\r\n");
        free(data);
        return NULL;
    }
    for (unsigned int i = 0; i < pages; i++) {
        void* page = alloc_page();
        if (!page) {
            serial_write_string("createUserProcess: alloc_page failed\r\n");
            break;
        }
        map_page(user_pml4, (uint64_t)(entry + i * PAGE_SIZE), (uint64_t)page, PAGE_PRESENT | PAGE_USER);
        memcpy(page, data + (i * PAGE_SIZE), (i == pages - 1) ? (size - i * PAGE_SIZE) : PAGE_SIZE);
    }
    kernel_cr3 = read_cr3();
    __asm__ volatile("mov %%rsp, %0" : "=r"(kernel_return_rsp));
    kernel_return_rip = &&kernel_resume;
    serial_write_string("creating userstack\n");
    createUserStack(user_pml4, (uint64_t)userstackAddr);
    dump_mapping((uint64_t *)user_pml4, (uint64_t)entry);
    Process** proc = processes;
    unsigned int i = 0;
    while (proc && i < MAX_PROCESSES) {
        proc+=sizeof(Process*);
        i++;
    }
    proc[i] = malloc(sizeof(Process));
    if (!proc[i]) {
        serial_write_string("createUserProcess: failed to allocate process struct\r\n");
        free(data);
        return NULL;
    }
    str_cp(proc[i]->name, name);
    proc[i]->pid = i;
    proc[i]->page_table = (uint64_t*)user_pml4;
    processes[i] = proc[i];
    return user_pml4;

kernel_resume:
    return NULL;
}

void startProcess(Process* proc) {
    if (!proc) {
        serial_write_string("startProcess: null proc\r\n");
        return;
    }
    toRing3_cr3((uint64_t)proc->page_table, (uint64_t)proc->entry, (uint64_t)proc->stack_top);
}

void pause_process(Process* proc, processRegs* regs) {
    if (!proc || !regs) {
        return;
    }

    // Save CPU state into process structure
    memcpy(&proc->regs, regs, sizeof(processRegs));

    proc->active = false;
}

void resume_process(Process* proc) {
    if (!proc) {
        serial_write_string("resume_process: null proc\r\n");
        return;
    }

    current_process = proc;

    // Switch to the process address space
    write_cr3((uint64_t)proc->page_table);

    // Restore full CPU state and return to userspace
    __asm__ volatile (
        // Restore general purpose registers
        "mov %0, %%rsp\n"

        "pop %%r15\n"
        "pop %%r14\n"
        "pop %%r13\n"
        "pop %%r12\n"
        "pop %%r11\n"
        "pop %%r10\n"
        "pop %%r9\n"
        "pop %%r8\n"

        "pop %%rsi\n"
        "pop %%rdi\n"
        "pop %%rbp\n"
        "pop %%rdx\n"
        "pop %%rcx\n"
        "pop %%rbx\n"
        "pop %%rax\n"

        // Return to saved RIP/CS/RFLAGS/RSP/SS
        "iretq\n"
        :
        : "r"(&proc->regs)
        : "memory"
    );
}