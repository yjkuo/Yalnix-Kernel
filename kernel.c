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
#include "io.h"


/* Kernel boot entry point */
extern void KernelStart (ExceptionInfo *info, unsigned int pmem_size, void *orig_brk, char **cmd_args) {

    int i;
    uintptr_t addr;
    uintptr_t ptaddr0;
    struct pte *pt;
    int text_npg;
    int heap_npg;
    int ret_val;

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
    ivt[TRAP_TTY_RECEIVE] = &TtyReceiveHandler;
    ivt[TRAP_TTY_TRANSMIT] = &TtyTransmitHandler;

    // Initializes the vector base register to point to the IVT
    WriteRegister(REG_VECTOR_BASE, (RCS421RegVal) ivt);

    // Initializes the free page list
    free_head = INT_MAX;

    // Adds pages below the kernel stack to the list of free pages (skipping the first MEM_INVALID_PAGES + 1 pages)
    addr = PMEM_BASE + MEM_INVALID_SIZE;
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

    // Book keeping for free page tables
    free_tables = calloc((pmem_size >> PAGESHIFT), sizeof(int));
    free_size = (pmem_size >> PAGESHIFT);

    // Creates the idle process
    InitProcess(&idle_pcb, READY, ptaddr0);
    active = &idle_pcb;

    // Loads the idle process
    char *args_idle[] = {"idle", NULL};
    LoadProgram("idle", args_idle, info);
    active = NULL;

    // Creates the init process
    init_pcb = (struct pcb*) malloc(sizeof(struct pcb));
    InitProcess(init_pcb, RUNNING, NewPageTable(ptaddr0));
    active = init_pcb;

    // Initializes the saved context for the idle process
    ContextSwitch(InitContext, &idle_pcb.ctx, init_pcb, NULL);

    // Stops executing if the first context switch has been completed
    if(first_return)
        return;
    first_return = 1;

    // Checks if an init program was specified
    if(cmd_args[0])
        ret_val = LoadProgram(cmd_args[0], cmd_args, info);

    // Else, loads the default 'init' program
    else {
        char *args_init[] = {"init", NULL};
        ret_val = LoadProgram("init", args_init, info);
    }

    if (ret_val == -1) {
        fprintf(stderr, "LoadProgram: program size too large for memory\n");
        Halt();
    }
}
uintptr_t GetTable() {

    size_t i;
    // 1 for page table at boundary, -1 for page table at middle of page
    for (i = 0; i < free_size; i++) {
        if (abs(free_tables[i]) == 1)
            break;
    }
    uintptr_t addr;
    if (i == free_size) {
        int pfn = GetPage();
        free_tables[pfn] = 1;
        addr = (PMEM_BASE + (pfn << PAGESHIFT) + PAGE_TABLE_SIZE);
    } else {
        addr = (PMEM_BASE + (i << PAGESHIFT) + ((free_tables[i] == 1) ? 0 : PAGE_TABLE_SIZE));
        free_tables[i] = 0;
    }
    
    return addr;
}

void FreeTable(uintptr_t addr) {
    struct pte *pt0;
    int pfn = (addr - PMEM_BASE) >> PAGESHIFT;
    TracePrintf(0, "Free page table at %p pfn %d\n", addr, pfn);

    if (abs(free_tables[pfn]) == 1) {
        BorrowPTE();
        pt1[borrowed_index].pfn = pfn;
        pt0 = (struct pte*) (borrowed_addr + ((addr - PMEM_BASE) & PAGEOFFSET));

        // Free the page table itself
        FreePage(PAGE_TABLE_LEN + borrowed_index, pfn);
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt0);

        // Frees the borrowed PTE
        ReleasePTE();

        free_tables[pfn] = 0;
        
    } else {
        free_tables[pfn] = (addr & PAGEOFFSET) ? -1 : 1;
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
        if((uintptr_t) addr > kernelbrk && (uintptr_t) addr < VMEM_1_LIMIT - PAGESIZE) {

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
        } else {

            // addr exceeds region 1 vitual memory
            TracePrintf(0, "SetKernelBrk: out of memory\n");
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

    // Flushes the page from the TLB
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

    // Temporarily buffers the current PTE if in use
    if(pt1[borrowed_index].valid) {
        pte_buffer[pte_count] = pt1[borrowed_index];
        pte_count++;
    }

    // Initializes the PTE
    pt1[borrowed_index].valid = 1;
    pt1[borrowed_index].kprot = PROT_READ | PROT_WRITE;
}


/* Frees a PTE borrowed from the region 1 page table */
void ReleasePTE () {

    // Frees the PTE
    pt1[borrowed_index].valid = 0;
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) borrowed_addr);

    // Restores a buffered PTE
    if(pte_count) {
        pte_count--;
        pt1[borrowed_index] = pte_buffer[pte_count];
    }

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
    uintptr_t ptaddr0;

    // Gets a new page for the page table
    ptaddr0 = GetTable();
    pfn = (ptaddr0 - PMEM_BASE) >> PAGESHIFT;

    // Accesses the region 0 page table of the old process
    BorrowPTE();
    pt1[borrowed_index].pfn = (addr - PMEM_BASE) >> PAGESHIFT;
    old_addr = (struct pte*) (borrowed_addr + ((addr - PMEM_BASE) & PAGEOFFSET));

    // Accesses the region 0 page table of the new process
    BorrowPTE();
    pt1[borrowed_index].pfn = pfn;
    new_addr = (struct pte*) (borrowed_addr + ((ptaddr0 - PMEM_BASE) & PAGEOFFSET));

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
    return ptaddr0;
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

/* Helps initialize a PCB */
int InitProcess (struct pcb *pcb, enum state_t state, uintptr_t addr) {

    // Initializes the PCB using the passed values
    pcb->pid = lastpid++;
    pcb->state = state;
    pcb->ptaddr0 = addr;
    pcb->used_npg = 0;
    pcb->sp = 0;
    pcb->brk = 0;
    pcb->clock_ticks = -1;
    pcb->input_buf.data = (char*) malloc(TERMINAL_MAX_LINE);
    pcb->input_buf.size = 0;
    pcb->output_buf.data = (char*) malloc(TERMINAL_MAX_LINE);
    pcb->output_buf.size = 0;
    pcb->parent = NULL;
    pcb->exit_status = 0;
    pcb->running_chd = (struct list*) malloc(sizeof(struct list));
    pcb->exited_chd = (struct queue*) malloc(sizeof(struct queue));

    if (!pcb->input_buf.data || !pcb->output_buf.data || !pcb->running_chd || !pcb->exited_chd) {
        return ERROR;
    }

    linit(pcb->running_chd);
    qinit(pcb->exited_chd);

    return 0;
}


/* Switches out the active process for a different process */
struct pcb* MoveProcesses (enum state_t new_state, void *new_dest) {

    struct pcb *new_process;
    struct queue *q;
    struct list *l;

    // Marks the current process as not running
    active->state = new_state;
    // CHANGED: idle should not put into ready queue
    if (active->pid > 0) {
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
    
    // CHANGED: No need to flush TLB here
    // WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    for(i = 0; i < PAGE_TABLE_LEN; i++) {
        if(pt0[i].valid) {
            pt0[i].kprot = PROT_WRITE;
            FreePage(i, pt0[i].pfn);
        }
    }

    // Free the page table itself
    // FreePage(PAGE_TABLE_LEN + borrowed_index, pt1[borrowed_index].pfn);
    FreeTable(pcb->ptaddr0);
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
    active->sp = (uintptr_t) cpp;

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

    // Initialize number of pages used by the program
    active->used_npg = text_npg + data_bss_npg + stack_npg + KERNEL_STACK_PAGES;

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