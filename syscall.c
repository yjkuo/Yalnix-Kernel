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

/* Returns PID of currently active process */
int KernelGetPid() {
    TracePrintf(0, "GetPid: '%d'\n", active->pid);
    return active->pid;
}

int KernelBrk(void *addr, void *sp) {
    TracePrintf(1, "Syscall brk Called\n");
    uintptr_t curbrk = active->brk;

    struct pte *ptr = (struct pte *)(VMEM_1_LIMIT - (PAGESIZE >> 1));
    ptr[PAGE_TABLE_LEN - 2].valid = 1;
    ptr[PAGE_TABLE_LEN - 2].kprot = PROT_READ | PROT_WRITE;
    ptr[PAGE_TABLE_LEN - 2].pfn = active->pfn0;
    struct pte *pt0 = (struct pte *) (VMEM_1_LIMIT - PAGESIZE);
    
    if ((uintptr_t)addr >= DOWN_TO_PAGE(sp)) {
        return ERROR;
    }
    if ((uintptr_t)addr > curbrk) {
        // grow the heap
        TracePrintf(1, "Grow user heap\n");
        int start = (curbrk - VMEM_0_BASE) >> PAGESHIFT;
        int end = (UP_TO_PAGE(addr) - VMEM_0_BASE) >> PAGESHIFT;
        if (end - start + 1 > free_npg) {
            // we don't have enough free pages, return error
            TracePrintf(1, "Warning: no more memory available\n");
            TracePrintf(1, "Avalable pages: %d\n", free_npg);
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
            pt0[i].pfn = GetPage();
        }
    } else {
        // shrink the heap
        TracePrintf(1, "Shrink user heap\n");
        int start = (UP_TO_PAGE(addr) - VMEM_0_BASE) >> PAGESHIFT;
        int end = (curbrk - VMEM_0_BASE) >> PAGESHIFT;

        int i;
        for (i = start; i < end; i++) {
            if (pt0[i].valid) {
                FreePage(i, pt0[i].pfn);
                pt0[i].valid = 0;
            }
        }
    }
    
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    active->brk = (uintptr_t)(UP_TO_PAGE(addr));
    return 0;
}

int KernelDelay(int clock_ticks) {
    return 0;
}

int KernelTtyRead() {
    return 0;
}

int KernelTtyWrite() {
    return 0;
}