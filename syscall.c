#include <stdlib.h>
#include <string.h>
#include <comp421/yalnix.h>

#include "syscall.h"
#include "queue.h"
#include "list.h"

int KernelFork(int caller_pid) {
    // if memory is not enough, return ERROR
    // TODO
    TracePrintf(0, "Entering Fork kernel call\n");
    // struct pcb *child_pcb = (struct pcb *) malloc(sizeof(struct pcb));
    // child_pcb->pid = lastpid++;
    // child_pcb->pfn0 = GetPage();
    // child_pcb->state = RUNNING;
    // child_pcb->next = NULL;
    // struct pte *pt1 = (struct pte *)(VMEM_1_LIMIT - (PAGESIZE >> 1));

    // // pte maps to new pages address
    // pt1[borrowed_index - 1].valid = 1;
    // pt1[borrowed_index - 1].kprot = PROT_READ | PROT_WRITE;
    // pt1[borrowed_index - 1].uprot = PROT_NONE;
    // pt1[borrowed_index - 1].pfn = active->pfn0;

    // // pte maps to new page table
    // pt1[borrowed_index - 2].valid = 1;
    // pt1[borrowed_index - 2].kprot = PROT_READ | PROT_WRITE;
    // pt1[borrowed_index - 2].pfn = child_pcb->pfn0;
    
    // // ContextSwitch(InitContext, &child_pcb->ctx, (void *) active, (void *) child_pcb);
    // void *to_addr = (void *)(VMEM_1_LIMIT - PAGESIZE * 3);
    // struct pte *pt0 = (struct pte *) (VMEM_1_LIMIT - PAGESIZE * 4);

    // // copy the page table first
    // memcpy(pt0, to_addr, PAGE_TABLE_SIZE);

    // int i;
    
    // // copy the entire process to new pages
    // for (i = 0; i < PAGE_TABLE_LEN; i++) {
    //     if (pt0[i].valid) {
    //         WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) to_addr);
    //         void *from_addr = (void *)(VMEM_0_BASE + (long) i * PAGESIZE);
    //         pt1[borrowed_index - 1].pfn = GetPage();
    //         memcpy(to_addr, from_addr, PAGESIZE);
    //     }
    // }
    
    // pt1[borrowed_index - 1].valid = 0;
    // pt1[borrowed_index - 2].valid = 0;
    // WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) to_addr);
    // WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt0);
    struct pcb *child_pcb = (struct pcb *) malloc(sizeof(struct pcb));
    child_pcb->pid = lastpid++;
    child_pcb->ptaddr0 = NewPageTable(active->ptaddr0);
    child_pcb->state = RUNNING;
    child_pcb->next = NULL;
    enq(&ready, child_pcb);
    ContextSwitch(InitContext, &child_pcb->ctx, NULL, (void *) active);
    TracePrintf(0, "current process pid: %d, parent process pid %d\n", active->pid, caller_pid);
    if (active->pid == caller_pid) 
        return child_pcb->pid;
    else {  
        WriteRegister(REG_PTR0, (RCS421RegVal) child_pcb->ptaddr0);
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
        return 0;
    }
}

int KernelExec(char *filename, char **argvec, ExceptionInfo *info) {
    LoadProgram(filename, argvec, info);
    return 0;
}

void KernelExit(int status) {
    TracePrintf(0, "Enter Exit kernel call\n");

    // pte maps to new pages address
    BorrowPTE();
    pt1[borrowed_index].pfn = active->ptaddr0 >> PAGESHIFT;
    struct pte *pt0 = (struct pte*) (borrowed_addr + ((active->ptaddr0 - PMEM_BASE) & PAGEOFFSET));

    int i;
    
    // copy the entire process to new pages
    for (i = 0; i < PAGE_TABLE_LEN - KERNEL_STACK_PAGES; i++) {
        if (pt0[i].valid) {
            pt0[i].kprot = PROT_READ | PROT_WRITE;
            FreePage(i, pt0[i].pfn);
            pt0[i].valid = 0;
        }
    }
    // int index = PAGE_TABLE_LEN + borrowed_index - 1;
    // FreePage(index, active->pfn0);
    ReleasePTE();
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt0);

    if (qempty(&ready)) {
        TracePrintf(0, "nobody in ready queue\n");
        ContextSwitch(Switch, &active->ctx, (void *) active, (void *) &idle_pcb);
    } else {
        struct pcb *newpcb = deq(&ready);
        struct pcb tmp;
        free(active);
        TracePrintf(0, "done free page\n");
        TracePrintf(0, "new pid %d, state %d\n", newpcb->pid, newpcb->state);
        ContextSwitch(Switch, &tmp.ctx, NULL, (void *) newpcb);
        TracePrintf(0, "Cannot happen\n");
    }
}

int KernelWait() {
    return 0;
}


/* Returns PID of currently active process */
int KernelGetPid () {
    TracePrintf(10, "process %d: executing GetPid()\n", active->pid);
    return active->pid;
}


/* Sets the break for the currently active process */
int KernelBrk (void *addr, void *sp) {
    TracePrintf(10, "process %d: executing Brk()\n", active->pid);

    uintptr_t curbrk;
    struct pte *pt0;
    int start_index, end_index;
    int i;

    // Checks if the specified break address is valid
    if((uintptr_t) addr < 0 || (uintptr_t) addr >= DOWN_TO_PAGE(sp)) {
        TracePrintf(10, "Brk: invalid address 0x%x", (uintptr_t) addr);
        return ERROR;
    }

    // Gets the current program break
    curbrk = active->brk;

    // Accesses the region 0 page table of the process
    BorrowPTE();
    pt1[borrowed_index].pfn = (active->ptaddr0 - PMEM_BASE) >> PAGESHIFT;
    pt0 = (struct pte*) (borrowed_addr + ((active->ptaddr0 - PMEM_BASE) & PAGEOFFSET));

    // Case 1 : grow the heap
    if((uintptr_t) addr > curbrk) {
        TracePrintf(10, "Brk: growing user heap\n");

        // Finds the range of pages to be allocated
        start_index = (curbrk - VMEM_0_BASE) >> PAGESHIFT;
        end_index = (UP_TO_PAGE(addr) - VMEM_0_BASE) >> PAGESHIFT;

        // Checks if there are enough free pages
        if(end_index - start_index + 1 > free_npg) {
            TracePrintf(10, "Brk: not enough free memory\n");
            return ERROR;
        }

        // Gets these pages
        for(i = start_index; i < end_index; i++) {
            pt0[i].valid = 1;
            pt0[i].pfn = GetPage();
            pt0[i].kprot = PROT_READ | PROT_WRITE;
            pt0[i].uprot = PROT_READ | PROT_WRITE;
        }
    }

    // Case 2 : shrink the heap
    if((uintptr_t) addr < curbrk) {
        TracePrintf(10, "Brk: shrinking user heap\n");

        // Finds the range of pages to be deallocated
        start_index = (UP_TO_PAGE(addr) - VMEM_0_BASE) >> PAGESHIFT;
        end_index = (curbrk - VMEM_0_BASE) >> PAGESHIFT;

        // Frees these pages
        for(i = start_index; i < end_index; i++) {
            pt0[i].kprot = PROT_WRITE;
            FreePage(i, pt0[i].pfn);
            pt0[i].valid = 0;
        }
    }

    // Flushes all region 0 entries from the TLB
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    // Updates the break for the current process
    active->brk = (uintptr_t)(UP_TO_PAGE(addr));

    return 0;
}


/* Blocks the currently active process for a while */
int KernelDelay (int clock_ticks) {
    TracePrintf(10, "process %d: executing Delay()\n", active->pid);

    // Checks for an invalid number of clock ticks
    if(clock_ticks < 0)
        return ERROR;

    // Checks for zero clock ticks
    if(clock_ticks == 0)
        return 0;

    // Sets the clock ticks for the current process
    active->clock_ticks = clock_ticks;

    // Marks the process as blocked
    active->state = BLOCKED;

    // Gets a process from the ready queue
    struct pcb *new_process = deq(&ready);

    // Switches to the new process
    // ContextSwitch(Switch, &active->ctxp, (void*) active, (void*) new_process);
    
    if (qempty(&ready)) {
        TracePrintf(0, "nobody in ready queue\n");
        ContextSwitch(Switch, &active->ctx, (void *) active, (void *) &idle_pcb);
    } else {
        struct pcb *pcb = deq(&ready);
        TracePrintf(0, "Take %d out from ready queue\n", pcb->pid);
        ContextSwitch(Switch, &active->ctx, (void *) active, (void *) pcb);
    }
    return 0;
}


int KernelTtyRead() {
    return 0;
}

int KernelTtyWrite() {
    return 0;
}