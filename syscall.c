#include <comp421/yalnix.h>

#include "syscall.h"
#include "queue.h"
#include "list.h"


int KernelFork() {
    return 0;
}

int KernelExec() {
    return 0;
}

void KernelExit() {

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
    ContextSwitch(Switch, &active->ctxp, (void*) active, (void*) new_process);

    return 0;
}


int KernelTtyRead() {
    return 0;
}

int KernelTtyWrite() {
    return 0;
}