#include <comp421/hardware.h>

#include "kernel.h"
#include "args.h"


/* Checks read access for string arguments */
int CheckString (char *str) {

    struct pte *pt0, entry;
    uintptr_t addr, curpage;
    int index;

    // Returns ERROR if the address doesn't lie in the region 0 address space
    if((uintptr_t) str < MEM_INVALID_SIZE || (uintptr_t) str >= KERNEL_STACK_BASE)
        return ERROR;

    // Accesses the region 0 page table of the process
    BorrowPTE();
    pt1[borrowed_index].pfn = (active->ptaddr0 - PMEM_BASE) >> PAGESHIFT;
    pt0 = (struct pte*) (borrowed_addr + ((active->ptaddr0 - PMEM_BASE) & PAGEOFFSET));

    // Checks the string byte-by-byte
    curpage = 0;
    while(1) {

        // Finds the page in which the address lies
        addr = DOWN_TO_PAGE(str);

        // Only proceeds if the page has not yet been checked
        if(curpage != addr) {
            curpage = addr;

            // Finds the corresponding page table entry
            index = (addr - VMEM_0_BASE) >> PAGESHIFT;
            entry = pt0[index];

            // Checks that the page is valid and has read access
            if(entry.valid == 0 || (entry.uprot & PROT_READ) != PROT_READ) {

                // Frees the borrowed PTE
                ReleasePTE();
                WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt0);

                // Returns ERROR
                return ERROR;
            }
        }

        // Stops if a null byte is encountered
        if(*str == '\0')
            break;
        str++;
    }

    // Frees the borrowed PTE
    ReleasePTE();
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt0);

    // Confirms that the argument is valid
    return 0;
}


/* Checks read/write access for buffer arguments */
int CheckBuffer (char *buf, int len, int prot) {

    struct pte *pt0, entry;
    uintptr_t addr, curpage;
    int i, index;

    // Returns ERROR if the address doesn't lie in the region 0 address space
    if((uintptr_t) buf < MEM_INVALID_SIZE || (uintptr_t) buf >= KERNEL_STACK_BASE)
        return ERROR;

    // Accesses the region 0 page table of the process
    BorrowPTE();
    pt1[borrowed_index].pfn = (active->ptaddr0 - PMEM_BASE) >> PAGESHIFT;
    pt0 = (struct pte*) (borrowed_addr + ((active->ptaddr0 - PMEM_BASE) & PAGEOFFSET));

    // Checks the buffer byte-by-byte
    curpage = 0;
    for(i = 0; i < len; i++) {

        // Finds the page in which the address lies
        addr = DOWN_TO_PAGE(buf + i);

        // Only proceeds if the page has not yet been checked
        if(curpage == addr)
            continue;
        curpage = addr;

        // Finds the corresponding page table entry
        index = (addr - VMEM_0_BASE) >> PAGESHIFT;
        entry = pt0[index];

        // Checks that the page is valid and has appropriate access
        if(entry.valid == 0 || (entry.uprot & prot) != prot) {

            // Frees the borrowed PTE
            ReleasePTE();
            WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt0);

            // Returns ERROR
            return ERROR;
        }
    }

    // Frees the borrowed PTE
    ReleasePTE();
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt0);

    // Confirms that the argument is valid
    return 0;
}