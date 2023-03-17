#include <comp421/yalnix.h>
#include "syscall.h"

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

int KernelGetPid() {
    TracePrintf(0, "GetPid: '%d'\n", active->pid);
    return active->pid;
}

int KernelBrk(void *addr, void *sp) {
    TracePrintf(1, "Syscall brk Called\n");
    uintptr_t curBrk = active->brk;
    struct pte *pt0 = (struct pte *) active->pageTable;
    
    if ((uintptr_t)addr >= DOWN_TO_PAGE(sp)) {
        return ERROR;
    }
    if ((uintptr_t)addr > curBrk) {
        // grow the heap
        TracePrintf(1, "Grow user heap\n");
        int start = (UP_TO_PAGE(curBrk) - VMEM_0_BASE) >> PAGESHIFT;
        int end = (UP_TO_PAGE(addr) - VMEM_0_BASE) >> PAGESHIFT;
        if (end - start > freePageCount) {
            // we don't have enough free pages, return error
            TracePrintf(1, "Warning: no more memory available\n");
            TracePrintf(1, "Avalable pages: %d\n", freePageCount);
            TracePrintf(1, "From page %d to page %d\n", start, end);
            return ERROR;
        }

        int i;
        for (i = start; i < end; i++) {
            if (pt0[i].valid) {
                // this shouldn't happen
                TracePrintf(1, "Error: valid page found above current brk\n");
                return ERROR;
            }
            pt0[i].valid = 1;
            pt0[i].kprot = PROT_READ | PROT_WRITE;
            pt0[i].uprot = PROT_READ | PROT_WRITE;
            pt0[i].pfn = getPage();
        }
    } else {
        // shrink the heap
        TracePrintf(1, "Shrink user heap\n");
        int start = (UP_TO_PAGE(addr) - VMEM_0_BASE) >> PAGESHIFT;
        int end = (UP_TO_PAGE(curBrk) - VMEM_0_BASE) >> PAGESHIFT;

        int i;
        for (i = start; i < end; i++) {
            if (pt0[i].valid) {
                freePage(i, pt0[i].pfn);
                pt0[i].valid = 0;
            }
        }
    }
    
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    active->brk = (uintptr_t)addr;
    return 0;
}

int KernelDelay(int clock_ticks) {
    active->state = BLOCKED;
    ContextSwitch
    return 0;
}

int KernelTtyRead() {
    return 0;
}

int KernelTtyWrite() {
    return 0;
}