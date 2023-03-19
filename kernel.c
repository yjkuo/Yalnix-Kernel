#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <comp421/loadinfo.h>
#include <comp421/hardware.h>
#include <comp421/yalnix.h>

#include "kernel.h"
#include "queue.h"

int runtime = 0;
// Stores the address of the region 1 page table
unsigned int pfn1;
struct pte *pt1;

// Stores the address and index of the borrowed PTE
void *borrowed_addr;
int borrowed_index;

// Manages a list of free pages
static unsigned int free_head;

// Keeps track of the last assigned PID
static unsigned int lastpid;

// Stores the current kernel break address
static uintptr_t kernelbrk;

// Flag of vitual memory enabled
static int vm_enabled;

/* Kernel boot entry point */
extern void KernelStart(ExceptionInfo *info, unsigned int pmem_size, void *orig_brk, char **cmd_args) {
    ivt_entry_t *ivt;
    struct pte cur_pte;
    int page_cnt = pmem_size >> PAGESHIFT;
    int i;
    int pfn;
    int prev = -1;
    uintptr_t addr;
    int kernelPageStart;
    struct pte *pt0;
    int text_cnt;
    int heap_cnt;
    kernelbrk = (uintptr_t) orig_brk;
    
    // Initializes the interrupt vector table entries
    ivt = (ivt_entry_t*) malloc(TRAP_VECTOR_SIZE * sizeof(ivt_entry_t));
    ivt[TRAP_KERNEL] = &kerHandler;
    ivt[TRAP_CLOCK] = &clkHandler;
    ivt[TRAP_ILLEGAL] = &illHandler;
    ivt[TRAP_MEMORY] = &memHandler;
    ivt[TRAP_MATH] = &mathHandler;
    ivt[TRAP_TTY_TRANSMIT] = &ttyXmitHandler;
    ivt[TRAP_TTY_RECEIVE] = &ttyRecvHandler;

    // Initializes the vector base register to point to the IVT
    WriteRegister(REG_VECTOR_BASE, (RCS421RegVal) ivt);
    
    //allocate memory for region 0 page table 
    pfn = MEM_INVALID_PAGES;
    addr = pfn << PAGESHIFT;
    pt0 = (struct pte *)addr;
    WriteRegister(REG_PTR0, (RCS421RegVal) pt0);

    //allocate memory for region 1 page table 
    addr = (pfn << PAGESHIFT) + (PAGESIZE >> 1);
    pt1 = (struct pte *)addr;
    WriteRegister(REG_PTR1, (RCS421RegVal) pt1);

    // construct free pages list
    for (i = MEM_INVALID_PAGES + 1; i < (KERNEL_STACK_BASE - PMEM_BASE) >> PAGESHIFT; i++) {     
        addr = (i << PAGESHIFT) + PMEM_BASE;
        *(int *)addr = prev;
        prev = i;
        free_npg++;
    } 

    kernelPageStart = i;

    for (i = (UP_TO_PAGE(kernelbrk) - PMEM_BASE) >> PAGESHIFT; i < page_cnt; i++) {
        addr = (i << PAGESHIFT) + PMEM_BASE;
        *(int *)addr = prev;
        prev = i;
        free_npg++;

    }

    free_head = prev;

    // create region 0 page table
    for (i = 0; i < PAGE_TABLE_LEN - KERNEL_STACK_PAGES; i++) {
        cur_pte.valid = 0;
        *(pt0++) = cur_pte;
    }

    for (i = 0; i < KERNEL_STACK_PAGES; i++) {
        cur_pte.valid = 1;
        cur_pte.pfn = kernelPageStart++;
        cur_pte.kprot = PROT_READ | PROT_WRITE;
        cur_pte.uprot = PROT_NONE;
        *(pt0++) = cur_pte;
    }
    
    // create region 1 page table
    text_cnt = ((long)&_etext - VMEM_1_BASE) >> PAGESHIFT;
    heap_cnt = (UP_TO_PAGE(kernelbrk) - VMEM_1_BASE) >> PAGESHIFT;

    for (i = 0; i < heap_cnt; i++) {
        cur_pte.valid = 1;
        cur_pte.pfn = kernelPageStart++;
        cur_pte.kprot = (i < text_cnt) ? (PROT_READ |  PROT_EXEC) : (PROT_READ | PROT_WRITE);
        cur_pte.uprot = PROT_NONE;
        *(pt1++) = cur_pte;
    }
    
    for (; i < PAGE_TABLE_LEN - 1; i++) {
        cur_pte.valid = 0;
        *(pt1++) = cur_pte;
    }
    // set top PTE of region 1 page table to map to page table location
    cur_pte.valid = 1;
    cur_pte.pfn = pfn;
    cur_pte.kprot = PROT_READ | PROT_WRITE;
    cur_pte.uprot = PROT_NONE;
    *pt1 = cur_pte;

    // Enables virtual memory
    WriteRegister(REG_VM_ENABLE, 1);
    vm_enabled = 1;

    qinit(&ready);
    qinit(&blocked);

    borrowed_addr = (void*)(VMEM_1_LIMIT - (PAGESIZE << 1));
    borrowed_index = PAGE_TABLE_LEN - 2;
    
    idle_pcb = (struct pcb *) malloc(sizeof(struct pcb));
    idle_pcb->pid = lastpid++;
    idle_pcb->pfn0 = pfn;
    idle_pcb->state = READY;
    idle_pcb->next = NULL;
    active = idle_pcb;
    char *args[] = {"idle", NULL};
    LoadProgram("idle", args, info, idle_pcb);

    struct pcb *init_pcb = (struct pcb*) malloc(sizeof(struct pcb));
    init_pcb->pid = lastpid++;
    init_pcb->pfn0 = GetPage();
    init_pcb->state = RUNNING;
    init_pcb->next = NULL;

    ContextSwitch(InitContext, &idle_pcb->ctx, NULL, (void *) init_pcb);
    if (runtime == 0) {
        // First time return from context switch
        runtime++;
        active = init_pcb;
        char *args2[] = {"init", NULL};
        LoadProgram("init", args2, info, init_pcb);
    }
}

extern int SetKernelBrk(void *addr) {
    TracePrintf(1, "Set kernel brk Called\n");
    if (vm_enabled == 1) {
        TracePrintf(1, "Set kernel brk after VM enabled\n");
        struct pte *pt1 = (struct pte *)(VMEM_1_LIMIT - (PAGESIZE >> 1));
        if ((uintptr_t) addr > kernelbrk) {
            // grow the kernel heap
            TracePrintf(1, "Grow heap\n");
            int start = (UP_TO_PAGE(kernelbrk) - VMEM_1_BASE) >> PAGESHIFT;
            int end = (UP_TO_PAGE(addr) - VMEM_1_BASE) >> PAGESHIFT;
            if (end - start - 1 > free_npg) {
                // we don't have enough free pages, return error
                return -1;
            }

            int i;
            for (i = start; i < end; i++) {
                pt1[i].valid = 1;
                pt1[i].kprot = PROT_READ | PROT_WRITE;
                pt1[i].uprot = PROT_NONE;
                pt1[i].pfn = GetPage();
            }
        }
        
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_1);
    }
    kernelbrk = (uintptr_t) addr;
    return 0;
}

/* Gets a free page from the free page list */
int GetPage () {

    // Gets the first available PFN from the list
    int pfn = free_head;

    // Borrows a PTE from the top of the region 1 page table
    struct pte *ptr = (struct pte *)(VMEM_1_LIMIT - (PAGESIZE >> 1));

    ptr[borrowed_index].valid = 1;
    ptr[borrowed_index].kprot = PROT_READ;
    ptr[borrowed_index].pfn = pfn;

    // Moves to the next page in the list
    unsigned int *addr = (unsigned int*) borrowed_addr;
    free_head = *addr;

    // Decrements the number of free pages
    free_npg--;

    // Returns the borrowed PTE
    ptr[borrowed_index].valid = 0;
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) borrowed_addr);

    // Returns the PFN
    return pfn;
}


/* Adds a page to the free page list */
void FreePage (int index, int pfn) {

    // Computes the virtual address of the page
    unsigned int *addr = (unsigned int*)((long)VMEM_0_BASE + index * PAGESIZE);

    // Adds the page to the list
    *addr = free_head;
    free_head = pfn;

    // Increments the number of free pages
    free_npg++;
}

/* Copy the current kernel stackt to new page one by one */
void CopyKernelStack(int to_pfn, int isNew) {

    struct pte *pt1 = (struct pte *)(VMEM_1_LIMIT - (PAGESIZE >> 1));

    // pte maps to new pages address
    pt1[borrowed_index - 1].valid = 1;
    pt1[borrowed_index - 1].kprot = PROT_READ | PROT_WRITE;
    pt1[borrowed_index - 1].uprot = PROT_NONE;

    // pte maps to to_pfn page table
    pt1[borrowed_index - 2].valid = 1;
    pt1[borrowed_index - 2].kprot = PROT_READ | PROT_WRITE;
    pt1[borrowed_index - 2].pfn = to_pfn;
    
    void *to_addr = (void *)(VMEM_1_LIMIT - PAGESIZE * 3);
    struct pte * newpt0 = (struct pte *) (VMEM_1_LIMIT - PAGESIZE * 4);
    int i;
    for (i = 0; i < KERNEL_STACK_PAGES; i++) {
        void *from_addr = (void *)(KERNEL_STACK_BASE + (long) i * PAGESIZE);
        if (isNew) {
            pt1[borrowed_index - 1].pfn = GetPage();
            newpt0[PAGE_TABLE_LEN - KERNEL_STACK_PAGES + i] = pt1[borrowed_index - 1];
        } else {
            pt1[borrowed_index - 1].pfn = newpt0[PAGE_TABLE_LEN - KERNEL_STACK_PAGES + i].pfn;
        }
        memcpy(to_addr, from_addr, PAGESIZE);
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) to_addr);
    }

    pt1[borrowed_index - 1].valid = 0;
    pt1[borrowed_index - 2].valid = 0;
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) to_addr);
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) newpt0);

    
}

/* Helps initialize a saved context */
SavedContext* InitContext (SavedContext *ctxp, void *p1, void *p2) {
    
    struct pcb *pcb2;
    uintptr_t addr;

    pcb2 = (struct pcb*) p2;

    // Copy current kernel stack to process 2 region 0 page table
    CopyKernelStack(pcb2->pfn0, 1);
    
    // Switches to the region 0 page table of process 2
    addr = pcb2->pfn0 << PAGESHIFT;
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
        enq(&blocked, pcb1);

    // Makes process 2 the active process
    pcb2->state = RUNNING;
    active = pcb2;
    
    // Switches to the region 0 page table of process 2
    addr = pcb2->pfn0 << PAGESHIFT;
    WriteRegister(REG_PTR0, (RCS421RegVal) addr);

    // Flushes all region 0 entries from the TLB
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    // Returns the saved context from the second process
    return &pcb2->ctx;
}


/* Loads a program in the current process's address space */
int LoadProgram (char *name, char **args, ExceptionInfo *info, struct pcb *pcbp) {

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

    // Borrows another PTE from the top of the region 1 page table so it won't overlap with GetPage()
    struct pte *ptr = (struct pte *)(VMEM_1_LIMIT - (PAGESIZE >> 1));
    ptr[borrowed_index - 1].valid = 1;
    ptr[borrowed_index - 1].kprot = PROT_READ | PROT_WRITE;
    ptr[borrowed_index - 1].pfn = active->pfn0;
    
    // Accesses the active procees region 0 page table
    pt0 = (struct pte*) (VMEM_1_LIMIT - PAGESIZE * 3);

    // Makes sure there will be enough physical memory to load the new program
    available_npg = free_npg;
    for(i = 0; i < PAGE_TABLE_LEN - KERNEL_STACK_PAGES; i++)
        if(pt0[i].valid)
            available_npg++;
    if(text_npg + data_bss_npg + stack_npg > available_npg) {
        TracePrintf(0, "LoadProgram: program '%s' size too large for PHYSICAL memory\n", name);
        free(argbuf);
        close(fd);
        ptr[borrowed_index - 1].valid = 0;
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) (VMEM_1_LIMIT - PAGESIZE * 3));
        return -1;
    }

    // Initializes SP for current process to (void*)cpp
    info->sp = (void*) cpp;

    // Frees all the old physical memory belonging to this process
    for(i = 0; i < PAGE_TABLE_LEN - KERNEL_STACK_PAGES; i++)
        if(pt0[i].valid) {
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
        pt0[i].kprot = PROT_READ | PROT_WRITE;
        pt0[i].uprot = PROT_READ | PROT_EXEC;
        pt0[i].pfn = GetPage();
    }

    // Fills in the page table with the right number of data and bss pages
    total_npg += data_bss_npg;
    for(; i < total_npg; i++) {
        pt0[i].valid = 1;
        pt0[i].kprot = PROT_READ | PROT_WRITE;
        pt0[i].uprot = PROT_READ | PROT_WRITE;
        pt0[i].pfn = GetPage();
    }

    // Initialize brk of the current process
    pcbp->brk = (uintptr_t)(i << PAGESHIFT);

    // Marks all pages in the subsequent gap as invalid
    total_npg = (USER_STACK_LIMIT >> PAGESHIFT) - stack_npg;
    for (; i < total_npg; i++)
        pt0[i].valid = 0;

    // Fills in the page table with the right number of user stack pages
    total_npg += stack_npg;
    for(; i < ((long) USER_STACK_LIMIT >> PAGESHIFT); i++) {
        pt0[i].valid = 1;
        pt0[i].kprot = PROT_READ | PROT_WRITE;
        pt0[i].uprot = PROT_READ | PROT_WRITE;
        pt0[i].pfn = GetPage();
    }

    // Flushes all region 0 entries from the TLB
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    
    // Reads the text and data from the file into memory
    if(read(fd, (void*) MEM_INVALID_SIZE, li.text_size + li.data_size) != li.text_size + li.data_size) {
        TracePrintf(0, "LoadProgram: couldn't read for '%s'\n", name);
        free(argbuf);
        close(fd);
        ptr[borrowed_index - 1].valid = 0;
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) (VMEM_1_LIMIT - PAGESIZE * 3));
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

    // Returns the borrowed PTE
    ptr[borrowed_index - 1].valid = 0;
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) (VMEM_1_LIMIT - PAGESIZE * 3));
    
    return 0;
}