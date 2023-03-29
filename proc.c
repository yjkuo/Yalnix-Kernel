#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <comp421/loadinfo.h>
#include <comp421/hardware.h>

#include "queue.h"
#include "list.h"
#include "proc.h"
#include "mmu.h"
#include "io.h"


/* Helps initialize a PCB */
int InitProcess (struct pcb *pcb, enum state_t state, uintptr_t addr) {

    // Initializes the PCB using the passed values
    pcb->pid = lastpid++;
    pcb->state = state;
    pcb->ptaddr0 = addr;
    pcb->used_npg = 0;
    pcb->user_stack_base = 0;
    pcb->brk = 0;
    pcb->clock_ticks = -1;
    pcb->parent = NULL;
    pcb->exit_status = 0;

    // Allocates memory for the tty buffers
    pcb->input_buf.data = (char*) malloc(TERMINAL_MAX_LINE);
    pcb->output_buf.data = (char*) malloc(TERMINAL_MAX_LINE);
    if(!pcb->input_buf.data || !pcb->output_buf.data)
        return ERROR;
    pcb->input_buf.size = 0;
    pcb->output_buf.size = 0;

    // Creates queues for child processes
    pcb->running_chd = (struct list*) malloc(sizeof(struct list));
    pcb->exited_chd = (struct queue*) malloc(sizeof(struct queue));
    if(!pcb->running_chd || !pcb->exited_chd)
        return ERROR;
    linit(pcb->running_chd);
    qinit(pcb->exited_chd);

    // Returns 0 if successful
    return 0;
}


/* Switches out the active process for a different process */
struct pcb* MoveProcesses (enum state_t new_state, void *new_dest) {

    struct pcb *new_process;
    struct queue *q;
    struct list *l;

    // Marks the current process as not running
    active->state = new_state;

    // Skips over the idle process
    if(active->pid > 0) {

        // If necessary, moves the active process to a different list or queue
        if(new_state == READY || new_state == READING || new_state == WRITING) {
            q = (struct queue*) new_dest;
            enq(q, active);
        }
        if(new_state == DELAYED || new_state == WAITING) {
            l = (struct list*) new_dest;
            insertl(l, active);
        }
    }

    // Gets a process from the ready queue (or switches to idle)
    if(qempty(ready))
        new_process = &idle_pcb;
    else
        new_process = deq(&ready);

    // Returns the new process
    return new_process;
}


/* Frees all the resources of a process except its PCB */
void RemoveProcess (struct pcb *pcb) {

    int i;
    struct pte *pt0;

    // Accesses the region 0 page table of the process
    BorrowPTE();
    pt1[borrowed_index].pfn = (pcb->ptaddr0 - PMEM_BASE) >> PAGESHIFT;
    pt0 = (struct pte*) (borrowed_addr + ((pcb->ptaddr0 - PMEM_BASE) & PAGEOFFSET));

    // Frees each page used by the process
    for(i = 0; i < PAGE_TABLE_LEN; i++) {
        if(pt0[i].valid) {
            pt0[i].kprot = PROT_WRITE;
            FreePage(i, pt0[i].pfn);
        }
    }

    // Free the page table itself
    FreePageTable(pcb->ptaddr0);
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt0);

    // Frees the borrowed PTE
    ReleasePTE();

    // Frees the tty buffers
    free(pcb->input_buf.data);
    free(pcb->output_buf.data);

    // Destroys the lists of children
    ldestroy(pcb->running_chd);
    free(pcb->running_chd);
    qdestroy(pcb->exited_chd);
    free(pcb->exited_chd);
}


/* Helps initialize a saved context */
SavedContext* InitContext (SavedContext *ctxp, void *proc, void *unused) {

    struct pcb *process;
    uintptr_t addr;

    // Gets the PCB for the process
    process = (struct pcb*) proc;

    // Copies the contents of the kernel stack
    CopyKernelStack(process->ptaddr0);

    // Switches to the region 0 page table of the process
    addr = process->ptaddr0;
    WriteRegister(REG_PTR0, (RCS421RegVal) addr);

    // Flushes all region 0 entries from the TLB
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    // Returns the saved context unmodified
    return ctxp;
}


/* Helps perform a context switch */
SavedContext* Switch (SavedContext *ctxp, void *p1, void *p2) {

    struct pcb *pcb1, *pcb2;
    uintptr_t addr;

    // Gets the PCBs for the two processes
    pcb1 = (struct pcb*) p1;
    pcb2 = (struct pcb*) p2;

    // Releases resources for a terminated process
    if(pcb1->state == TERMINATED)
        RemoveProcess(pcb1);

    // Makes the second process active
    pcb2->state = RUNNING;
    active = pcb2;

    // Switches to the region 0 page table of the second process
    addr = pcb2->ptaddr0;
    WriteRegister(REG_PTR0, (RCS421RegVal) addr);

    // Flushes all region 0 entries from the TLB
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    // Returns the saved context from the second process
    return &pcb2->ctx;
}


/* Loads a program in the current process's address space */
int LoadProgram (char *name, char **args, ExceptionInfo *info) {

    int fd;
    int status;
    struct loadinfo li;
    char *cp;
    char *cp2;
    char **cpp;
    char *argbuf;
    int i;
    unsigned long argcount;
    int size;
    int text_npg;
    int data_bss_npg;
    int stack_npg;
    int available_npg;
    int total_npg;
    struct pte *pt0;

    // Prints out the program and its arguments
    TracePrintf(0, "LoadProgram '%s', args %p\n", name, args);

    // Attempts to open the program file
    if((fd = open(name, O_RDONLY)) < 0) {
        TracePrintf(0, "LoadProgram: can't open file '%s'\n", name);
        return -1;
    }

    // Attempts to load the program info
    status = LoadInfo(fd, &li);
    TracePrintf(0, "LoadProgram: LoadInfo status %d\n", status);
    switch(status) {
        case LI_SUCCESS:
            break;
        case LI_FORMAT_ERROR:
            TracePrintf(0, "LoadProgram: '%s' not in Yalnix format\n", name);
            close(fd);
            return -1;
        case LI_OTHER_ERROR:
            TracePrintf(0, "LoadProgram: '%s' other error\n", name);
            close(fd);
            return -1;
        default:
            TracePrintf(0, "LoadProgram: '%s' unknown error\n", name);
            close(fd);
            return -1;
    }
    TracePrintf(0, "text_size 0x%lx, data_size 0x%lx, bss_size 0x%lx\n", li.text_size, li.data_size, li.bss_size);
    TracePrintf(0, "entry 0x%lx\n", li.entry);

    // Calculates the number of bytes are needed to hold the arguments on the new stack
    size = 0;
    for(i = 0; args[i] != NULL; i++)
	    size += strlen(args[i]) + 1;
    argcount = i;
    TracePrintf(0, "LoadProgram: size %d, argcount %d\n", size, argcount);

    // Saves the arguments in a separate buffer in region 1
    cp = argbuf = (char*) malloc(size);
    if (!cp) {
        TracePrintf(0, "LoadProgram: malloc returns NULL\n");
        free(argbuf);
        close(fd);
        return -1;
    }
    for(i = 0; args[i] != NULL; i++) {
        strcpy(cp, args[i]);
        cp += strlen(cp) + 1;
    }

    // Computes the addresses at which the arguments and argv pointers are stored
    cp = ((char*) USER_STACK_LIMIT) - size;
    cpp = (char**) ((unsigned long)cp & (-1 << 4));
    cpp = (char**) ((unsigned long)cpp - ((argcount + 4) * sizeof(void*)));

    // Calculates the number of text, data, and stack pages
    text_npg = li.text_size >> PAGESHIFT;
    data_bss_npg = UP_TO_PAGE(li.data_size + li.bss_size) >> PAGESHIFT;
    stack_npg = (USER_STACK_LIMIT - DOWN_TO_PAGE(cpp)) >> PAGESHIFT;
    TracePrintf(0, "LoadProgram: text_npg %d, data_bss_npg %d, stack_npg %d\n", text_npg, data_bss_npg, stack_npg);

    // Makes sure we have enough virtual memory to fit everything within the size of a page table
    if(MEM_INVALID_PAGES + text_npg + data_bss_npg + 1 + stack_npg + 1 + KERNEL_STACK_PAGES > PAGE_TABLE_LEN) {
        TracePrintf(0, "LoadProgram: program '%s' size too large for VIRTUAL memory\n", name);
        free(argbuf);
        close(fd);
        return -1;
    }

    // Borrows a PTE from the top of the region 1 page table
    BorrowPTE();
    pt1[borrowed_index].pfn = (active->ptaddr0 - PMEM_BASE) >> PAGESHIFT;

    // Accesses the region 0 page table
    pt0 = (struct pte*) (borrowed_addr + ((active->ptaddr0 - PMEM_BASE) & PAGEOFFSET));

    // Makes sure there will be enough physical memory to load the new program
    available_npg = free_npg;
    for(i = 0; i < PAGE_TABLE_LEN - KERNEL_STACK_PAGES; i++)
        if(pt0[i].valid)
            available_npg++;
    if(text_npg + data_bss_npg + stack_npg > available_npg) {
        TracePrintf(0, "LoadProgram: program '%s' size too large for PHYSICAL memory\n", name);
        free(argbuf);
        close(fd);
        ReleasePTE();
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt0);
        return -1;
    }

    // Initializes SP for current process to (void*)cpp
    info->sp = (void*) cpp;

    // Frees all the old physical memory belonging to this process
    for(i = 0; i < PAGE_TABLE_LEN - KERNEL_STACK_PAGES; i++)
        if(pt0[i].valid) {
            pt0[i].kprot = PROT_WRITE;
            FreePage(i, pt0[i].pfn);
            pt0[i].valid = 0;
        }

    // Marks the first MEM_INVALID_PAGES PTEs in the region 0 page table invalid
    total_npg = MEM_INVALID_PAGES;
    for(i = 0; i < total_npg; i++)
        pt0[i].valid = 0;

    // Fills in the page table with the right number of text pages
    total_npg += text_npg;
    for(; i < total_npg; i++) {
        pt0[i].valid = 1;
        pt0[i].pfn = GetPage();
        pt0[i].kprot = PROT_READ | PROT_WRITE;
        pt0[i].uprot = PROT_READ | PROT_EXEC;
    }

    // Fills in the page table with the right number of data and bss pages
    total_npg += data_bss_npg;
    for(; i < total_npg; i++) {
        pt0[i].valid = 1;
        pt0[i].pfn = GetPage();
        pt0[i].kprot = PROT_READ | PROT_WRITE;
        pt0[i].uprot = PROT_READ | PROT_WRITE;
    }

    // Initializes the program break for the current process
    active->brk = (uintptr_t)(VMEM_0_BASE + (total_npg << PAGESHIFT));

    // Marks all pages in the subsequent gap as invalid
    total_npg = (USER_STACK_LIMIT >> PAGESHIFT) - stack_npg;
    for(; i < total_npg; i++)
        pt0[i].valid = 0;

    // Stores the base of the user stack
    active->user_stack_base = (uintptr_t)(VMEM_0_BASE + (total_npg << PAGESHIFT));

    // Fills in the page table with the right number of user stack pages
    total_npg += stack_npg;
    for(; i < total_npg; i++) {
        pt0[i].valid = 1;
        pt0[i].pfn = GetPage();
        pt0[i].kprot = PROT_READ | PROT_WRITE;
        pt0[i].uprot = PROT_READ | PROT_WRITE;
    }

    // Initializes the number of pages used by the process
    active->used_npg = text_npg + data_bss_npg + stack_npg + KERNEL_STACK_PAGES;

    // Flushes all region 0 entries from the TLB
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    // Reads the text and data from the file into memory
    if(read(fd, (void*) MEM_INVALID_SIZE, li.text_size + li.data_size) != li.text_size + li.data_size) {
        TracePrintf(0, "LoadProgram: couldn't read for '%s'\n", name);
        free(argbuf);
        close(fd);
        ReleasePTE();
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt0);
        return -2;
    }

    // Closes the program file
    close(fd);

    // Sets the page table entries for the program text to be readable and executable
    for(i = MEM_INVALID_PAGES; i < MEM_INVALID_PAGES + text_npg; i++)
        pt0[i].kprot = PROT_READ | PROT_EXEC;

    // Flushes all region 0 entries from the TLB
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    // Zeros out the bss
    memset((void*)(MEM_INVALID_SIZE + li.text_size + li.data_size), '\0', li.bss_size);

    // Sets the entry point for ExceptionInfo
    info->pc = (void*) li.entry;

    // Builds the argument list on the new stack
    *cpp++ = (char*) argcount;
    cp2 = argbuf;
    for(i = 0; i < argcount; i++) {
        *cpp++ = cp;
        strcpy(cp, cp2);
        cp += strlen(cp) + 1;
        cp2 += strlen(cp2) + 1;
    }
    free(argbuf);
    *cpp++ = NULL;
    *cpp++ = NULL;
    *cpp++ = 0;

    // Initialize all registers for the current process to 0
    for(i = 0; i < NUM_REGS; i++)
        info->regs[i] = 0;
    info->psr = 0;

    // Returns the borrowed PTE and flushes the entry for the region 0 page table
    ReleasePTE();
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt0);

    return 0;
}