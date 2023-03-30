#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <comp421/hardware.h>
#include <comp421/yalnix.h>

#include "mmu.h"


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

    // Flushes the address from the TLB
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

        // Flushes the invalidated TLB entry
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) borrowed_addr);
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


/* Gets a physical address for a new page table */
uintptr_t GetPageTable () {

    int i;
    uintptr_t addr;
    unsigned int pfn;

    // Looks for a free slot for the new page table
    for(i = 0; i < free_ntbl; i++)
        if(abs(free_tables[i]) == 1)
            break;

    // Case 1 : no free slot exists
    if(i == free_ntbl) {
        pfn = GetPage();

        // Puts the new page table at the middle address
        addr = PMEM_BASE + (pfn << PAGESHIFT) + PAGE_TABLE_SIZE;

        // Marks the starting address as unused
        free_tables[pfn] = 1;
    }

    // Case 2 : a free slot exists
    else {

        // Computes the address of the page table
        if(free_tables[i] == 1)
            addr = PMEM_BASE + (i << PAGESHIFT);
        else
            addr = PMEM_BASE + (i << PAGESHIFT) + PAGE_TABLE_SIZE;

        // Makes the slot unavailable
        free_tables[i] = 0;
    }

    // Returns the address
    return addr;
}


/* Initializes a new page table */
int InitPageTable (uintptr_t addr) {

    uintptr_t ptaddr0;
    unsigned int pfn;
    struct pte *old_addr, *new_addr;
    int i;
    void *virtual_addr;

    // Gets a new slot for the page table
    ptaddr0 = GetPageTable();
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


/* Frees up a physical address being used by a page table */
void FreePageTable (uintptr_t addr) {

    int pfn;
    struct pte *pt;

    // Finds the index into the list of free page tables
    pfn = (addr - PMEM_BASE) >> PAGESHIFT;

    // Case 1 : half of the page is already free
    if(abs(free_tables[pfn]) == 1) {

        // Makes the page table accessible
        BorrowPTE();
        pt1[borrowed_index].pfn = pfn;
        pt = (struct pte*) (borrowed_addr + ((addr - PMEM_BASE) & PAGEOFFSET));

        // Frees the page table
        FreePage(PAGE_TABLE_LEN + borrowed_index, pfn);
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt);

        // Frees the borrowed PTE
        ReleasePTE();

        // Marks the page as unused
        free_tables[pfn] = 0;
    }

    // Case 2 : the entire page is full
    else {

        // Stores whether the starting address or the middle address is now unused
        if(addr & PAGEOFFSET)
            free_tables[pfn] = -1;
        else
            free_tables[pfn] = 1;
    }
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