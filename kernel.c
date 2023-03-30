#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <comp421/loadinfo.h>
#include <comp421/hardware.h>

#include "kernel.h"
#include "queue.h"
#include "list.h"
#include "proc.h"
#include "mmu.h"
#include "io.h"


/* Kernel boot entry point */
extern void KernelStart (ExceptionInfo *info, unsigned int pmem_size, void *orig_brk, char **cmd_args) {

    int i;
    uintptr_t addr;
    uintptr_t ptaddr0;
    struct pte *pt;
    int text_npg;
    int heap_npg;
    int retval;

    // Stores the original kernel break
    kernelbrk = (uintptr_t) orig_brk;
    TracePrintf(0, "KernelStart: set kernel break to %p\n", kernelbrk);

    // Initializes the IVT entries
    for(i = 0; i < TRAP_VECTOR_SIZE; i++)
        ivt[i] = NULL;
    ivt[TRAP_KERNEL] = &KernelHandler;
    ivt[TRAP_CLOCK] = &ClockHandler;
    ivt[TRAP_ILLEGAL] = &IllegalHandler;
    ivt[TRAP_MEMORY] = &MemoryHandler;
    ivt[TRAP_MATH] = &MathHandler;
    ivt[TRAP_TTY_RECEIVE] = &TtyReceiveHandler;
    ivt[TRAP_TTY_TRANSMIT] = &TtyTransmitHandler;

    // Initializes the vector base register to point to the IVT
    WriteRegister(REG_VECTOR_BASE, (RCS421RegVal) ivt);

    // Initializes the free page list
    free_head = INT_MAX;

    // Adds pages below the kernel stack to the list of free pages (skipping the first MEM_INVALID_PAGES + 1 pages)
    addr = PMEM_BASE + MEM_INVALID_SIZE + PAGESIZE;
    for(; addr < KERNEL_STACK_BASE; addr += PAGESIZE) {
        *(unsigned int*) addr = free_head;
        free_head = (addr - PMEM_BASE) >> PAGESHIFT;
        free_npg++;
    }

    // Adds pages above the kernel break to the list of free pages
    addr = UP_TO_PAGE(kernelbrk);
    for(; addr < PMEM_BASE + pmem_size; addr += PAGESIZE) {
        *(unsigned int*) addr = free_head;
        free_head = (addr - PMEM_BASE) >> PAGESHIFT;
        free_npg++;
    }

    // Allocates memory for an initial region 0 page table
    ptaddr0 = PMEM_BASE + MEM_INVALID_SIZE + (PAGESIZE >> 1);
    pt = (struct pte*) ptaddr0;
    WriteRegister(REG_PTR0, (RCS421RegVal) pt);

    // Creates the region 0 page table
    for(i = 0; i < PAGE_TABLE_LEN - KERNEL_STACK_PAGES; i++)
        pt[i].valid = 0;
    for(; i < PAGE_TABLE_LEN; i++) {
        pt[i].valid = 1;
        pt[i].pfn = i;
        pt[i].kprot = PROT_READ | PROT_WRITE;
        pt[i].uprot = PROT_NONE;
    }

    // Calculates the number of text and heap pages used by the kernel
    text_npg = ((uintptr_t) &_etext - VMEM_1_BASE) >> PAGESHIFT;
    heap_npg = (UP_TO_PAGE(kernelbrk) - VMEM_1_BASE) >> PAGESHIFT;
    TracePrintf(0, "KernelStart: text_npg %d, heap_npg %d\n", text_npg, heap_npg);

    // Allocates memory for the region 1 page table
    ptaddr1 = PMEM_BASE + MEM_INVALID_SIZE;
    pt = (struct pte*) ptaddr1;
    WriteRegister(REG_PTR1, (RCS421RegVal) pt);

    // Creates the region 1 page table
    for(i = 0; i < text_npg; i++) {
        pt[i].valid = 1;
        pt[i].pfn = PAGE_TABLE_LEN + i;
        pt[i].kprot = PROT_READ | PROT_EXEC;
        pt[i].uprot = PROT_NONE;
    }
    for(; i < heap_npg; i++) {
        pt[i].valid = 1;
        pt[i].pfn = PAGE_TABLE_LEN + i;
        pt[i].kprot = PROT_READ | PROT_WRITE;
        pt[i].uprot = PROT_NONE;
    }
    for(; i < PAGE_TABLE_LEN; i++)
        pt[i].valid = 0;

    // Sets the top PTE of the region 1 page table to map to itself
    pt[PAGE_TABLE_LEN - 1].valid = 1;
    pt[PAGE_TABLE_LEN - 1].pfn = ptaddr1 >> PAGESHIFT;
    pt[PAGE_TABLE_LEN - 1].kprot = PROT_READ | PROT_WRITE;
    pt[PAGE_TABLE_LEN - 1].uprot = PROT_NONE;

    // Enables virtual memory
    WriteRegister(REG_VM_ENABLE, 1);
    vm_enabled = 1;

    // Stores the virtual address of the region 1 page table
    pt1 = (struct pte*)(VMEM_1_LIMIT - PAGESIZE);

    // Initializes the borrowed address and index
    borrowed_addr = (void*)(VMEM_1_LIMIT - PAGESIZE);
    borrowed_index = PAGE_TABLE_LEN - 1;

    // Adds the first MEM_INVALID_PAGES pages to the list of free pages
    for(addr = PMEM_BASE; addr < PMEM_BASE + MEM_INVALID_SIZE; addr += PAGESIZE) {

        // Puts the address in the region 1 page table
        BorrowPTE();
        pt1[borrowed_index].pfn = (addr - PMEM_BASE) >> PAGESHIFT;

        // Adds it to the list
        *(unsigned int*) borrowed_addr = free_head;
        free_head = (addr - PMEM_BASE) >> PAGESHIFT;
        free_npg++;

        // Removes the address from the page table
        ReleasePTE();
    }

    // Prints out the total number of free pages
    TracePrintf(0, "KernelStart: free_npg %d\n", free_npg);

    // Initializes the ready and blocked queues
    qinit(&ready);
    linit(&blocked);

    // Initializes the terminals
    for(i = 0; i < NUM_TERMINALS; i++) {

        // Creates an initial input buffer
        term[i].input_bufs = NULL;
        term[i].lines = 0;

        // Initializes the waiting queues
        term[i].read_procs = (struct queue*) malloc(sizeof(struct queue));
        qinit(term[i].read_procs);
        term[i].write_procs = (struct queue*) malloc(sizeof(struct queue));
        qinit(term[i].write_procs);

        // Marks the terminal as ready
        term[i].term_state = FREE;       
    }

    // Initializes the heap of free page tables
    free_ntbl = pmem_size >> PAGESHIFT;
    free_tables = calloc(free_ntbl, sizeof(int));

    // Creates the idle process
    InitProcess(&idle_pcb, READY, ptaddr0);
    active = &idle_pcb;

    // Loads the idle process
    char *args_idle[] = {"idle", NULL};
    LoadProgram("idle", args_idle, info);
    active = NULL;

    // Creates the init process
    init_pcb = (struct pcb*) malloc(sizeof(struct pcb));
    InitProcess(init_pcb, RUNNING, InitPageTable(ptaddr0));
    active = init_pcb;

    // Initializes the saved context for the idle process
    ContextSwitch(InitContext, &idle_pcb.ctx, init_pcb, NULL);

    // Stops executing if the first context switch has been completed
    if(first_return)
        return;
    first_return = 1;

    // Checks if an init program was specified
    if(cmd_args[0])
        retval = LoadProgram(cmd_args[0], cmd_args, info);

    // Else, loads the default 'init' program
    else {
        char *args_init[] = {"init", NULL};
        retval = LoadProgram("init", args_init, info);
    }

    // Checks if the program was correctly loaded
    if(retval < 0) {
        TracePrintf(0, "KernelStart: program could not be loaded\n");
        fprintf(stderr, "KernelStart: program could not be loaded\n");
        Halt();
    }
}


/* Sets the break address for the kernel */
extern int SetKernelBrk (void *addr) {
    TracePrintf(0, "executing SetKernelBrk() with addr %p\n", addr);

    int start_index, end_index;
    int i;

    // Case 1 : virtual memory has already been enabled
    if(vm_enabled) {
        TracePrintf(0, "SetKernelBrk: VM enabled\n");

        // Confirms that the specified address is higher than the current break
        if((uintptr_t) addr > kernelbrk && (uintptr_t) addr < VMEM_1_LIMIT) {

            // Finds the range of pages to be allocated
            start_index = (UP_TO_PAGE(kernelbrk) - VMEM_1_BASE) >> PAGESHIFT;
            end_index = (UP_TO_PAGE(addr) - VMEM_1_BASE) >> PAGESHIFT;

            // Checks if there are enough free pages
            if(end_index - start_index > free_npg) {
                TracePrintf(0, "SetKernelBrk: not enough free pages\n");
                return -1;
            }

            // Gets these pages
            for(i = start_index; i < end_index; i++) {
                pt1[i].valid = 1;
                pt1[i].pfn = GetPage();
                pt1[i].kprot = PROT_READ | PROT_WRITE;
                pt1[i].uprot = PROT_NONE;
            }
        }

        // Else, logs an error
        else {
            TracePrintf(0, "SetKernelBrk: invalid address\n");
            return -1;
        }

        // Flushes all region 1 entries from the TLB
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
    }

    // Case 2 : virtual memory has not yet been enabled
    else
        TracePrintf(0, "SetKernelBrk: VM not enabled\n");

    // Updates the current break
    kernelbrk = (uintptr_t) addr;

    return 0;
}