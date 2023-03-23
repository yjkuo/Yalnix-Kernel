#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <comp421/loadinfo.h>
#include <comp421/hardware.h>
#include <comp421/yalnix.h>

#include "kernel.h"
#include "queue.h"
#include "list.h"


/* Kernel boot entry point */
extern void KernelStart (ExceptionInfo *info, unsigned int pmem_size, void *orig_brk, char **cmd_args) {

    int i;
    uintptr_t addr;
    uintptr_t ptaddr0;
    struct pte *pt;
    int text_npg;
    int heap_npg;

    // Stores the original kernel break
    kernelbrk = (uintptr_t) orig_brk;
    TracePrintf(0, "KernelStart: set kernel break to 0x%x\n", kernelbrk);

    // Initializes the IVT entries
    for(i = 0; i < TRAP_VECTOR_SIZE; i++)
        ivt[i] = NULL;
    ivt[TRAP_KERNEL] = &KernelHandler;
    ivt[TRAP_CLOCK] = &ClockHandler;
    ivt[TRAP_ILLEGAL] = &IllegalHandler;
    ivt[TRAP_MEMORY] = &MemoryHandler;
    ivt[TRAP_MATH] = &MathHandler;
    ivt[TRAP_TTY_TRANSMIT] = &TtyTransmitHandler;
    ivt[TRAP_TTY_RECEIVE] = &TtyReceiveHandler;

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

    // Initializes the ready and blocked queues
    qinit(&ready);
    linit(&blocked);

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

    // Creates the idle process
    idle_pcb.pid = lastpid++;
    idle_pcb.state = READY;
    idle_pcb.ptaddr0 = ptaddr0;
    idle_pcb.clock_ticks = -1;
    idle_pcb.next = NULL;

    // Temporarily sets idle to be the active process
    active = &idle_pcb;

    // Loads the idle process
    char *args_idle[] = {"idle", NULL};
    LoadProgram("idle", args_idle, info);
    active = NULL;

    // Creates the init process
    init_pcb.pid = lastpid++;
    init_pcb.state = RUNNING;
    init_pcb.ptaddr0 = NewPageTable(ptaddr0);
    init_pcb.clock_ticks = -1;
    init_pcb.next = NULL;

    // Adds idle to the ready queue
    enq(&ready, &idle_pcb);

    // Makes init the currently active process
    active = &init_pcb;

    // Initializes the saved context switch for the idle process
    ContextSwitch(InitContext, &idle_pcb.ctx, NULL, &init_pcb);

    // Stops executing if the first context switch has been completed
    if(first_return)
        return;
    first_return = 1;

    // Checks if an init program was specified
    if(cmd_args[0])
        LoadProgram(cmd_args[0], cmd_args, info);

    // Else, loads the default 'init' program
    else {
        char *args_init[] = {"init", NULL};
        LoadProgram("init", args_init, info);
    }
}


/* Sets the break address for the kernel */
extern int SetKernelBrk (void *addr) {
    TracePrintf(0, "executing SetKernelBrk()\n");

    int start_index, end_index;
    int i;

    // Case 1 : virtual memory has already been enabled
    if(vm_enabled) {
        TracePrintf(0, "SetKernelBrk: VM enabled\n");

        // Confirms that the specified address is higher than the current break
        if((uintptr_t) addr > kernelbrk) {

            // Finds the range of pages to be allocated
            start_index = (UP_TO_PAGE(kernelbrk) - VMEM_1_BASE) >> PAGESHIFT;
            end_index = (UP_TO_PAGE(addr) - VMEM_1_BASE) >> PAGESHIFT;

            // Checks if there are enough free pages
            if(end_index - start_index + 5 > free_npg) {
                TracePrintf(0, "SetKernelBrk: not enough free memory\n");
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


/* Gets a free page from the free page list */
int GetPage () {

    // Gets the first available PFN from the list
    int pfn = free_head;

    // Borrows a PTE from the top of the region 1 page table
    BorrowPTE();
    pt1[borrowed_index].pfn = pfn;

    // Moves to the next page in the list
    unsigned int *addr = (unsigned int*) borrowed_addr;
    free_head = *addr;

    // Decrements the number of free pages
    free_npg--;

    // Frees the borrowed PTE
    ReleasePTE();

    // Returns the PFN
    return pfn;
}


/* Adds a page to the free page list */
void FreePage (int index, int pfn) {

    // Computes the virtual address of the page
    unsigned int *addr = (unsigned int*)((uintptr_t) VMEM_0_BASE + index * PAGESIZE);

    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) addr);
    // Adds the page to the list
    *addr = free_head;
    free_head = pfn;

    // Increments the number of free pages
    free_npg++;
}


/* Borrows a new PTE from the region 1 page table */
void BorrowPTE () {

    // Updates the borrowed address and index
    borrowed_addr = (void*)((uintptr_t) borrowed_addr - PAGESIZE);
    borrowed_index--;

    // Initializes the PTE
    pt1[borrowed_index].valid = 1;
    pt1[borrowed_index].kprot = PROT_READ | PROT_WRITE;
}


/* Frees a PTE borrowed from the region 1 page table */
void ReleasePTE () {

    // Frees the PTE
    pt1[borrowed_index].valid = 0;
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) borrowed_addr);

    // Updates the borrowed address and index
    borrowed_addr = (void*)((uintptr_t) borrowed_addr + PAGESIZE);
    borrowed_index++;
}


/* Creates a new page table */
int NewPageTable (uintptr_t addr) {

    unsigned int pfn;
    int i;
    struct pte *old_addr, *new_addr;
    void *virtual_addr;

    // Gets a new page for the page table
    pfn = GetPage();

    // Accesses the region 0 page table of the old process
    BorrowPTE();
    pt1[borrowed_index].pfn = (addr - PMEM_BASE) >> PAGESHIFT;
    old_addr = (struct pte*) (borrowed_addr + ((addr - PMEM_BASE) & PAGEOFFSET));

    // Accesses the region 0 page table of the new process
    BorrowPTE();
    pt1[borrowed_index].pfn = pfn;
    new_addr = (struct pte*) borrowed_addr;

    // Copies the page table over
    memcpy(new_addr, old_addr, PAGE_TABLE_SIZE);

    // Gets new pages for the process space
    for(i = 0; i < PAGE_TABLE_LEN - KERNEL_STACK_PAGES; i++) {

        // Checks if the page is valid
        if(!new_addr[i].valid)
            continue;

        // Gets a new page
        new_addr[i].pfn = GetPage();

        // Accesses this new page
        BorrowPTE();
        pt1[borrowed_index].pfn = new_addr[i].pfn;

        // Accesses the old page
        virtual_addr = (void*)((uintptr_t) VMEM_0_BASE + i * PAGESIZE);

        // Copies the page over
        memcpy(borrowed_addr, virtual_addr, PAGESIZE);

        // Removes all TLB references to the new page
        ReleasePTE();
    }

    // Frees the borrowed PTEs
    ReleasePTE();
    ReleasePTE();

    // Flushes the invalidated TLB entry
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) old_addr);

    // Returns the physical address of the new table
    return (PMEM_BASE + (pfn << PAGESHIFT));
}


/* Copies the kernel stack to a new page table */
void CopyKernelStack (uintptr_t addr) {

    int i;
    struct pte *pt_addr;
    void *virtual_addr;

    // Accesses the region 0 page table of the process
    BorrowPTE();
    pt1[borrowed_index].pfn = (addr - PMEM_BASE) >> PAGESHIFT;
    pt_addr = (struct pte*) (borrowed_addr + ((addr - PMEM_BASE) & PAGEOFFSET));

    // Gets new pages for the kernel stack
    for(i = PAGE_TABLE_LEN - KERNEL_STACK_PAGES; i < PAGE_TABLE_LEN; i++) {
    
        // Accesses a new page
        BorrowPTE();
        pt1[borrowed_index].pfn = GetPage();

        // Copies the old page over
        virtual_addr = (void*)((uintptr_t) VMEM_0_BASE + i * PAGESIZE);
        memcpy(borrowed_addr, virtual_addr, PAGESIZE);

        // Replaces the old page with the new one
        pt_addr[i].pfn = pt1[borrowed_index].pfn;

        // Removes all TLB references to the new page
        ReleasePTE();
    }

    // Frees the borrowed PTE
    ReleasePTE();

    // Flushes the invalidated TLB entry
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt_addr);
}


/* Helps initialize a saved context */
SavedContext* InitContext (SavedContext *ctxp, void *p1, void *p2) {

    struct pcb *process;
    uintptr_t addr;

    // Gets the PCB for the process
    process = (struct pcb*) p2;

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

    // Moves the first process to a different queue
    if(pcb1->state == READY)
        enq(&ready, pcb1);
    if(pcb1->state == BLOCKED)
        insertl(&blocked, pcb1);

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
            pt0[i].kprot = PROT_READ | PROT_WRITE;
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

    // Initialize the program break for the current process
    active->brk = (uintptr_t)(total_npg << PAGESHIFT);

    // Marks all pages in the subsequent gap as invalid
    total_npg = (USER_STACK_LIMIT >> PAGESHIFT) - stack_npg;
    for(; i < total_npg; i++)
        pt0[i].valid = 0;

    // Fills in the page table with the right number of user stack pages
    total_npg += stack_npg;
    for(; i < total_npg; i++) {
        pt0[i].valid = 1;
        pt0[i].pfn = GetPage();
        pt0[i].kprot = PROT_READ | PROT_WRITE;
        pt0[i].uprot = PROT_READ | PROT_WRITE;
    }

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