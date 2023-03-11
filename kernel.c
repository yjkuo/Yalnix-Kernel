#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <comp421/loadinfo.h>
#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include "kernel.h"

extern void KernelStart(ExceptionInfo *info, unsigned int pmem_size, void *orig_brk, char **cmd_args) {
    struct pte cur_pte;
    int page_cnt = pmem_size >> PAGESHIFT;
    int i;
    int pfn;
    int prev = -1;
    uintptr_t addr;
    int kernelPageStart;
    struct pte *pt0, *pt1;
    int text_cnt;
    int heap_cnt;

    // initialize interrupt vector table
    ivt_entry_t *ivt = (ivt_entry_t *) calloc(TRAP_VECTOR_SIZE, sizeof(ivt_entry_t));
    ivt[TRAP_KERNEL] = &kerHandler;
    ivt[TRAP_CLOCK] = &clkHandler;
    ivt[TRAP_ILLEGAL] = &illHandler;
    ivt[TRAP_MEMORY] = &memHandler;
    ivt[TRAP_MATH] = &mathHandler;
    ivt[TRAP_TTY_TRANSMIT] = &ttyXmitHandler;
    ivt[TRAP_TTY_RECEIVE] = &ttyRecvHandler;
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
        freePageCount++;
    } 

    kernelPageStart = i;

    for (i = (UP_TO_PAGE(orig_brk) - PMEM_BASE) >> PAGESHIFT; i < page_cnt; i++) {
        addr = (i << PAGESHIFT) + PMEM_BASE;
        *(int *)addr = prev;
        prev = i;
        freePageCount++;

    }

    freePageHead = prev;

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
    heap_cnt = (UP_TO_PAGE(orig_brk) - VMEM_1_BASE) >> PAGESHIFT;

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

    // ============ Enable VM ===============
    WriteRegister(REG_VM_ENABLE, 1);
    
    // struct pcb idle_pcb;
    // idle_pcb.pid = 0;
    // idle_pcb.next = NULL;
    // idle_pcb.state = RUNNING;
    // active = idle_pcb;

    // char *args[] = {"idle", NULL};
    // LoadProgram("idle", args, info);

    struct pcb init_pcb;
    init_pcb.pid = 1;
    init_pcb.next = NULL;
    init_pcb.state = RUNNING;
    active = init_pcb;

    char *args2[] = {"init", NULL};
    LoadProgram("init", args2, info);
}

extern int SetKernelBrk(void *addr) {
    
    return 0;
}

// get a free page from free pages list
int getPage () {
    int pfn = freePageHead;
    // borrow a pte from top of region 1 PT
    struct pte *ptr = (struct pte *)(VMEM_1_LIMIT - (PAGESIZE >> 1));
    ptr[PAGE_TABLE_LEN - 2].valid = 1;
    ptr[PAGE_TABLE_LEN - 2].kprot = PROT_READ | PROT_WRITE;
    ptr[PAGE_TABLE_LEN - 2].pfn = pfn;
    uintptr_t addr = (VMEM_1_LIMIT - (PAGESIZE << 1));
    freePageHead = *(int *)(addr);
    freePageCount--;

    // remember to return the pte
    ptr[PAGE_TABLE_LEN - 2].valid = 0;
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) addr);

    return pfn;
}

void freePage(int pteIndex, int pfn) {
    uintptr_t addr = (VMEM_0_BASE + pteIndex * PAGESIZE);
    *(int *)(addr) = freePageHead;
    freePageHead = pfn;
}

SavedContext *MySwitchFunc(SavedContext *ctxp, void *p1, void *p2) {

}

int LoadProgram(char *name, char **args, ExceptionInfo *info) {
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

    TracePrintf(0, "LoadProgram '%s', args %p\n", name, args);

    if ((fd = open(name, O_RDONLY)) < 0) {
	TracePrintf(0, "LoadProgram: can't open file '%s'\n", name);
	return (-1);
    }

    status = LoadInfo(fd, &li);
    TracePrintf(0, "LoadProgram: LoadInfo status %d\n", status);
    switch (status) {
	case LI_SUCCESS:
	    break;
	case LI_FORMAT_ERROR:
	    TracePrintf(0,
		"LoadProgram: '%s' not in Yalnix format\n", name);
	    close(fd);
	    return (-1);
	case LI_OTHER_ERROR:
	    TracePrintf(0, "LoadProgram: '%s' other error\n", name);
	    close(fd);
	    return (-1);
	default:
	    TracePrintf(0, "LoadProgram: '%s' unknown error\n", name);
	    close(fd);
	    return (-1);
    }
    TracePrintf(0, "text_size 0x%lx, data_size 0x%lx, bss_size 0x%lx\n",
	li.text_size, li.data_size, li.bss_size);
    TracePrintf(0, "entry 0x%lx\n", li.entry);

    /*
     *  Figure out how many bytes are needed to hold the arguments on
     *  the new stack that we are building.  Also count the number of
     *  arguments, to become the argc that the new "main" gets called with.
     */
    size = 0;
    for (i = 0; args[i] != NULL; i++) {
	size += strlen(args[i]) + 1;
    }
    argcount = i;
    TracePrintf(0, "LoadProgram: size %d, argcount %d\n", size, argcount);

    /*
     *  Now save the arguments in a separate buffer in Region 1, since
     *  we are about to delete all of Region 0.
     */
    cp = argbuf = (char *)malloc(size);
    for (i = 0; args[i] != NULL; i++) {
	strcpy(cp, args[i]);
	cp += strlen(cp) + 1;
    }
  
    /*
     *  The arguments will get copied starting at "cp" as set below,
     *  and the argv pointers to the arguments (and the argc value)
     *  will get built starting at "cpp" as set below.  The value for
     *  "cpp" is computed by subtracting off space for the number of
     *  arguments plus 4 (for the argc value, a 0 (AT_NULL) to
     *  terminate the auxiliary vector, a NULL pointer terminating
     *  the argv pointers, and a NULL pointer terminating the envp
     *  pointers) times the size of each (sizeof(void *)).  The
     *  value must also be aligned down to a multiple of 8 boundary.
     */
    cp = ((char *)USER_STACK_LIMIT) - size;
    cpp = (char **)((unsigned long)cp & (-1 << 4));	/* align cpp */
    cpp = (char **)((unsigned long)cpp - ((argcount + 4) * sizeof(void *)));

    text_npg = li.text_size >> PAGESHIFT;
    data_bss_npg = UP_TO_PAGE(li.data_size + li.bss_size) >> PAGESHIFT;
    stack_npg = (USER_STACK_LIMIT - DOWN_TO_PAGE(cpp)) >> PAGESHIFT;

    TracePrintf(0, "LoadProgram: text_npg %d, data_bss_npg %d, stack_npg %d\n",
	text_npg, data_bss_npg, stack_npg);

    /*
     *  Make sure we have enough *virtual* memory to fit everything within
     *  the size of a page table, including leaving at least one page
     *  between the heap and the user stack
     */
    if (MEM_INVALID_PAGES + text_npg + data_bss_npg + 1 + stack_npg +
	1 + KERNEL_STACK_PAGES > PAGE_TABLE_LEN) {
	TracePrintf(0,
	    "LoadProgram: program '%s' size too large for VIRTUAL memory\n",
	    name);
	free(argbuf);
	close(fd);
	return (-1);
    }

    /*
     *  And make sure there will be enough *physical* memory to
     *  load the new program.
     */
    /* The new program will require text_npg pages of text,
    * data_bss_npg pages of data/bss, and stack_npg pages of
    * stack.  In checking that there is enough free physical
    * memory for this, be sure to allow for the physical memory
    * pages already allocated to this process that will be
    * freed below before we allocate the needed pages for
    * the new program being loaded.
    */
    struct pte *pt0 = (struct pte *)(VMEM_1_LIMIT - PAGESIZE);
    int availableCount = freePageCount;

    for (i = 0; i < PAGE_TABLE_LEN - KERNEL_STACK_PAGES; i++) {
        if (pt0[i].valid)
            availableCount++;
    }
    if (text_npg + data_bss_npg + stack_npg > availableCount) {
	TracePrintf(0,
	    "LoadProgram: program '%s' size too large for PHYSICAL memory\n",
	    name);
	free(argbuf);
	close(fd);
	return (-1);
    }

    /* Initialize sp for the current process to (void *)cpp.
     * The value of cpp was initialized above.
     */
    info->sp = (void *)cpp;

    /*
     *  Free all the old physical memory belonging to this process,
     *  but be sure to leave the kernel stack for this process (which
     *  is also in Region 0) alone.
     */
    /* Loop over all PTEs for the current process's Region 0,
     * except for those corresponding to the kernel stack (between
     * address KERNEL_STACK_BASE and KERNEL_STACK_LIMIT).  For
     * any of these PTEs that are valid, free the physical memory
     * memory page indicated by that PTE's pfn field.  Set all
     * of these PTEs to be no longer valid.
     */
    for (i = 0; i < PAGE_TABLE_LEN - KERNEL_STACK_PAGES; i++) {
        if (pt0[i].valid) {
            freePage(i, pt0[i].pfn);
            pt0[i].valid = 0;
        }
    }

    /*
     *  Fill in the page table with the right number of text,
     *  data+bss, and stack pages.  We set all the text pages
     *  here to be read/write, just like the data+bss and
     *  stack pages, so that we can read the text into them
     *  from the file.  We then change them read/execute.
     */

    /*
    * Leave the first MEM_INVALID_PAGES number of PTEs in the
    * Region 0 page table unused (and thus invalid)
    */
    int limit = MEM_INVALID_PAGES;
    for (i = 0; i < limit; i++) {
        pt0[i].valid = 0;
    }
    /* First, the text pages */
    limit += text_npg;
    for (; i < limit; i++) {
        pt0[i].valid = 1;
        pt0[i].kprot = PROT_READ | PROT_WRITE;
        pt0[i].uprot = PROT_READ | PROT_EXEC;
        pt0[i].pfn = getPage();
    }

    /* Then the data and bss pages */
    limit += data_bss_npg;
    for (; i < limit; i++) {
        pt0[i].valid = 1;
        pt0[i].kprot = PROT_READ | PROT_WRITE;
        pt0[i].uprot = PROT_READ | PROT_WRITE;
        pt0[i].pfn = getPage();
    }

    /* And finally the user stack pages */
    for (; i < ((long) USER_STACK_LIMIT >> PAGESHIFT) - stack_npg; i++) {
        pt0[i].valid = 0;
    }
    
    for (; i < ((long) USER_STACK_LIMIT >> PAGESHIFT); i++) {
        pt0[i].valid = 1;
        pt0[i].kprot = PROT_READ | PROT_WRITE;
        pt0[i].uprot = PROT_READ | PROT_WRITE;
        pt0[i].pfn = getPage();
    }

    /*
     *  All pages for the new address space are now in place.  Flush
     *  the TLB to get rid of all the old PTEs from this process, so
     *  we'll be able to do the read() into the new pages below.
     */
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    /*
     *  Read the text and data from the file into memory.
     */
    if (read(fd, (void *)MEM_INVALID_SIZE, li.text_size+li.data_size)
	!= li.text_size+li.data_size) {
	TracePrintf(0, "LoadProgram: couldn't read for '%s'\n", name);
	free(argbuf);
	close(fd);
    /*
	* Since we are returning -2 here, this should mean to
	* the rest of the kernel that the current process should
	* be terminated with an exit status of ERROR reported
	* to its parent process.
    */
	return (-2);
    }

    close(fd);			/* we've read it all now */

    /*
     *  Now set the page table entries for the program text to be readable
     *  and executable, but not writable.
     */
    for (i = MEM_INVALID_PAGES; i < MEM_INVALID_PAGES + text_npg; i++) {
        pt0[i].kprot = PROT_READ | PROT_EXEC;
    }

    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

    /*
     *  Zero out the bss
     */
    memset((void *)(MEM_INVALID_SIZE + li.text_size + li.data_size),
	'\0', li.bss_size);

    /*
     *  Set the entry point in the ExceptionInfo.
     */
    info->pc = (void *)li.entry;

    /*
     *  Now, finally, build the argument list on the new stack.
     */
    *cpp++ = (char *)argcount;		/* the first value at cpp is argc */
    cp2 = argbuf;
    for (i = 0; i < argcount; i++) {      /* copy each argument and set argv */
	*cpp++ = cp;
	strcpy(cp, cp2);
	cp += strlen(cp) + 1;
	cp2 += strlen(cp2) + 1;
    }
    free(argbuf);
    *cpp++ = NULL;	/* the last argv is a NULL pointer */
    *cpp++ = NULL;	/* a NULL pointer for an empty envp */
    *cpp++ = 0;		/* and terminate the auxiliary vector */

    /*
     *  Initialize all regs[] registers for the current process to 0,
     *  initialize the PSR for the current process also to 0.  This
     *  value for the PSR will make the process run in user mode,
     *  since this PSR value of 0 does not have the PSR_MODE bit set.
     */
    for (i = 0; i < NUM_REGS; i++)
        info->regs[i] = 0;
    info->psr = 0;
    
    return (0);
}