#include <stdlib.h>
#include <string.h>
#include <comp421/yalnix.h>

#include "syscall.h"

int KernelFork(int caller_pid) {
    TracePrintf(10, "process %d: executing Fork()\n", active->pid);

    // if memory is not enough, return ERROR
    if (active->used_npg > free_npg) {
        TracePrintf(10, "Fork: not enough free memory\n");
        return ERROR;
    }
    
    struct pcb *child_pcb = (struct pcb *) malloc(sizeof(struct pcb));
    InitPCB(child_pcb, READY, NewPageTable(active->ptaddr0));
    child_pcb->brk = active->brk;
    child_pcb->used_npg = active->used_npg;
    child_pcb->parent = active;

    enq(&ready, child_pcb);

    insertl(&active->children, child_pcb);
    
    ContextSwitch(InitContext, &child_pcb->ctx, NULL, (void *) active);
    TracePrintf(0, "current process pid: %d, parent process pid %d\n", active->pid, caller_pid);
    if (active->pid == caller_pid) {    
        return child_pcb->pid;
    } else {  
        WriteRegister(REG_PTR0, (RCS421RegVal) child_pcb->ptaddr0);
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
        return 0;
    }
}

int KernelExec(char *filename, char **argvec, ExceptionInfo *info) {
    TracePrintf(10, "process %d: executing Exec()\n", active->pid);

    int ret_val = LoadProgram(filename, argvec, info);

    if (ret_val == -1)
        return ERROR;

    if (ret_val == -2) {
        TracePrintf(10, "Exec: load program failed\n");
        return ERROR;
    }

    return 0;
}

void KernelExit(int status) {
    TracePrintf(10, "process %d: executing Exit()\n", active->pid);
    
    // tell all children parent exits
    exitl(&active->children);

    // tell parent my exit status
    if (active->parent) {
        deletel(&active->parent->children, active);
        enq(&active->parent->exited_children, active);
        active->exit_status = status;
        
        // check if there's parent waiting
        if (active->parent->state == WAITING) {
            TracePrintf(10, "Child process %d exit before parent process %d execute Wait()\n", active->pid, active->parent->pid);
            active->parent->state = READY;
            enq(&ready, active->parent);
        }
    }

    if (lempty(blocked) && qempty(&ready)) {
        TracePrintf(10, "Exit: No remaining processes, shut down the kernel...\n");
        Halt();
    }
    struct pcb *new_process = qempty(&ready) ? &idle_pcb : deq(&ready);
    active->state = TERMINATED;
    ContextSwitch(Switch, &active->ctx, (void *) active, (void *) new_process);
    
}

int KernelWait(int *status_ptr) {

    if (lempty(active->children) && qempty(&active->exited_children)) 
        return ERROR;

    if (!lempty(active->children)) {
        struct pcb *new_process = qempty(&ready) ? &idle_pcb : deq(&ready);
        active->state = WAITING;
        ContextSwitch(Switch, &active->ctx, (void *) active, (void *) new_process); 
    }

    if (!qempty(&active->exited_children)) {
        struct pcb *child = deq(&active->exited_children);
        free(child);
        *status_ptr = child->exit_status;
        return child->pid;
    }
    return ERROR;
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
    TracePrintf(0, "Current process brk at %p\n", curbrk);
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
        active->used_npg += end_index - start_index;
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
        active->used_npg -= end_index - start_index;
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

    // // Gets a process from the ready queue
    // struct pcb *new_process = deq(&ready);

    // // Switches to the new process
    // ContextSwitch(Switch, &active->ctxp, (void*) active, (void*) new_process);
    
    struct pcb *new_process = qempty(&ready) ? &idle_pcb : deq(&ready);
    
    ContextSwitch(Switch, &active->ctx, (void *) active, (void *) new_process);
    
    return 0;
}


int KernelTtyRead() {
    return 0;
}

int KernelTtyWrite() {
    return 0;
}