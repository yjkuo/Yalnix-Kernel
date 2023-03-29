#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <comp421/yalnix.h>

#include "syscall.h"
#include "kernel.h"
#include "queue.h"
#include "list.h"
#include "io.h"


/* Checks read access for string arguments */
int CheckString (char *str) {

    struct pte *pt0, entry;
    uintptr_t addr;
    int index;

    // Returns ERROR if the address lies in the region 0 address space
    if((uintptr_t) str < 0 || (uintptr_t) str >= VMEM_0_LIMIT)
        return ERROR;

    // Accesses the region 0 page table of the process
    BorrowPTE();
    pt1[borrowed_index].pfn = (active->ptaddr0 - PMEM_BASE) >> PAGESHIFT;
    pt0 = (struct pte*) (borrowed_addr + ((active->ptaddr0 - PMEM_BASE) & PAGEOFFSET));

    // Checks the string byte-by-byte
    while(1) {

        // Finds the corresponding page table entry
        addr = DOWN_TO_PAGE(str);
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

        // Stops if a null byte is encountered
        str++;
        if(*str == '\0')
            break;
    }

    // Frees the borrowed PTE
    ReleasePTE();
    WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt0);

    // Confirms that the argument is valid
    return 0;
}


/* Checks read/write access for buffers */
int CheckBuffer (char *buf, int len, int prot) {

    struct pte *pt0, entry;
    uintptr_t addr;
    int i, index;

    // Returns ERROR if the address lies in the region 0 address space
    if((uintptr_t) buf < 0 || (uintptr_t) buf >= VMEM_0_LIMIT)
        return ERROR;

    // Accesses the region 0 page table of the process
    BorrowPTE();
    pt1[borrowed_index].pfn = (active->ptaddr0 - PMEM_BASE) >> PAGESHIFT;
    pt0 = (struct pte*) (borrowed_addr + ((active->ptaddr0 - PMEM_BASE) & PAGEOFFSET));

    // Checks the buffer byte-by-byte
    for(i = 0; i < len; i++) {

        // Finds the corresponding page table entry
        addr = DOWN_TO_PAGE(buf + i);
        index = (addr - VMEM_0_BASE) >> PAGESHIFT;
        entry = pt0[index];

        // Checks that the page is valid and has appropriate access
        if(entry.valid == 0 || (entry.uprot & prot) == 0) {

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


/* Creates a new process */
int KernelFork (int caller_pid) {
    TracePrintf(10, "process %d: executing Fork()\n", active->pid);

    int retval;

    // Checks if there is enough free memory
    if(active->used_npg + 1 > free_npg) {
        TracePrintf(10, "Fork: not enough free pages\n");
        return ERROR;
    }

    // Creates a PCB for the child process
    struct pcb *child_process = (struct pcb*) malloc(sizeof(struct pcb));
    retval = InitProcess(child_process, READY, NewPageTable(active->ptaddr0));

    if (!child_process || retval) {
        TracePrintf(10, "Fork: kernel out of memory\n");
        return ERROR;
    }
    
    // Remembers the memory usage of the child process
    child_process->used_npg = active->used_npg;
    child_process->brk = active->brk;
    child_process->sp = active->sp;

    // Marks the two processes as parent and child
    insertl(active->running_chd, child_process);
    child_process->parent = active;

    // Adds the child process to the ready queue
    enq(&ready, child_process);

    // Initializes the saved context for the new process
    WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
    ContextSwitch(InitContext, &child_process->ctx, (void*) active, NULL);

    // Case 1 : parent process
    if(active->pid == caller_pid)

        // Returns the PID of the child to the parent
        return child_process->pid;

    // Case 2 : child process
    else {

        // Switches to the region 0 page table of the child process
        WriteRegister(REG_PTR0, (RCS421RegVal) child_process->ptaddr0);
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);

        // Returns 0 to the child
        return 0;
    }
}


/* Replaces the currently running program in the calling process’s memory */
int KernelExec (char *filename, char **argvec, ExceptionInfo *info) {
    TracePrintf(10, "process %d: executing Exec()\n", active->pid);

    int i, retval;

    // Validates the arguments
    if(!filename || CheckString(filename)){
        TracePrintf(10, "Exec: invalid filename pointer %p\n", (uintptr_t) filename);
        return ERROR;
    }
    if (!argvec) {
        TracePrintf(10, "Exec: invalid argument pointer %p\n", (uintptr_t) argvec);
        return ERROR;
    }
    for(i = 0; argvec[i]; i++)
        if(CheckString(argvec[i])) {
            TracePrintf(10, "Exec: invalid argument pointer %p\n", (uintptr_t) argvec[i]);
            return ERROR;
        }

    // Loads the specified program
    retval = LoadProgram(filename, argvec, info);

    // Case 1 : current process is still runnable
    if(retval == -1) {
        TracePrintf(10, "Exec: load program failed, current process intact\n");
        fprintf(stderr, "Exec: load program failed, current process intact\n");
    
        // Returns an error
        return ERROR;
    }

    // Case 2 : current process is no longer runnable
    if(retval == -2) {
        TracePrintf(10, "Exec: load program failed, current process lost\n");
        fprintf(stderr, "Exec: load program failed, current process lost\n");

        // Terminates the current process
        KernelExit(ERROR);
    }

    return 0;
}


/* Terminates the current process */
void KernelExit (int status) {
    TracePrintf(10, "process %d: executing Exit()\n", active->pid);

    struct pcb *new_process;
    int remaining = 0, i;

    // Stores the exit status in the PCB
    active->exit_status = status;

    // Informs all running children that their parent is exiting
    exitl(active->running_chd);

    // Informs a running parent that its child is exiting
    if(active->parent) {

        // Moves the child to a different queue
        deletel(active->parent->running_chd, active);
        enq(active->parent->exited_chd, active);

        // Unblocks the parent process if waiting
        if(active->parent->state == WAITING) {
            active->parent->state = READY;
            deletel(&blocked, active->parent);
            enq(&ready, active->parent);
        }
    }

    // Checks if there are still processes in the ready queue
    if(!qempty(ready))
        remaining = 1;

    // Checks if there are still processes in the blocked list
    if(!lempty(blocked))
        remaining = 1;

    // Checks if there are still processes in any of the reading queues
    for(i = 0; i < NUM_TERMINALS; i++)
        if(!qempty(*term[i].read_procs))
            remaining = 1;

    // Checks if there are still processes in any of the writing queues
    for(i = 0; i < NUM_TERMINALS; i++)
        if(!qempty(*term[i].write_procs))
            remaining = 1;

    // Shuts down the kernel if there are no remaining processes except idle
    if(remaining == 0) {
        TracePrintf(10, "Exit: no remaining processes; shutting down the kernel ...\n");
        Halt();
    }

    // Gets a new process to make active, and switches to it
    new_process = MoveProcesses(TERMINATED, NULL);
    ContextSwitch(Switch, &active->ctx, (void*) active, (void*) new_process);
}


/* Returns the process ID and exit status of a child */
int KernelWait (int *status_ptr) {
    TracePrintf(10, "process %d: executing Wait()\n", active->pid);

    struct pcb *new_process, *child;
    int pid;

    // Validates the argument
    if(!status_ptr || CheckBuffer((char*) status_ptr, sizeof(int), PROT_READ | PROT_WRITE)) {
        TracePrintf(10, "Wait: invalid status pointer %p\n", (uintptr_t) status_ptr);
        return ERROR;
    }

    // Checks if the process has any remaining children
    if(lempty(*active->running_chd) && qempty(*active->exited_chd))
        return ERROR;

    // Blocks if all of the process' children are still running
    if(qempty(*active->exited_chd)) {

        // Gets a new process to make active, and switches to it
        new_process = MoveProcesses(WAITING, &blocked);
        ContextSwitch(Switch, &active->ctx, (void*) active, (void*) new_process);
    }
    
    // Gets an exited child process
    child = deq(active->exited_chd);

    // Stores its exit status and PID
    *status_ptr = child->exit_status;
    pid = child->pid;

    // Releases the child's PCB
    free(child);

    // Returns the child's PID
    return pid;
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

    // Validates the argument
    if(!addr || (uintptr_t) addr < 0 || (uintptr_t) addr >= DOWN_TO_PAGE(sp) - PAGESIZE) {
        TracePrintf(10, "Brk: invalid address %p\n", (uintptr_t) addr);
        return ERROR;
    }

    // Gets the current program break
    curbrk = active->brk;
    TracePrintf(10, "Brk: current process break at %p\n", curbrk);

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
        if(end_index - start_index > free_npg) {
            TracePrintf(10, "Brk: not enough free pages\n");
            return ERROR;
        }

        // Gets these pages
        for(i = start_index; i < end_index; i++) {
            pt0[i].valid = 1;
            pt0[i].pfn = GetPage();
            pt0[i].kprot = PROT_READ | PROT_WRITE;
            pt0[i].uprot = PROT_READ | PROT_WRITE;
        }

        // Updates the memory usage of the current process
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

        // Updates the memory usage of the current process
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

    struct pcb *new_process;

    // Validates the argument
    if(clock_ticks < 0)
        return ERROR;

    // Checks for zero clock ticks
    if(clock_ticks == 0)
        return 0;

    // Sets the clock ticks for the current process
    active->clock_ticks = clock_ticks;

    // Gets a new process to make active, and switches to it
    new_process = MoveProcesses(DELAYED, &blocked);
    ContextSwitch(Switch, &active->ctx, (void*) active, (void*) new_process);

    // Returns 0 once unblocked
    return 0;
}


/* Reads data from a terminal */
int KernelTtyRead (int tty_id, void *buf, int len) {
    TracePrintf(10, "process %d: executing TtyRead()\n", active->pid);

    struct terminal *tm;
    int num_chars, i;
    struct pcb *new_process;

    // Validates the arguments
    if(tty_id < 0 || tty_id >= NUM_TERMINALS) {
        TracePrintf(10, "TtyRead: invalid terminal id %d\n", tty_id);
        return ERROR;
    }
    if(len < 0 || len > TERMINAL_MAX_LINE) {
        TracePrintf(10, "TtyRead: invalid buffer length %d\n", len);
        return ERROR;
    }
    if(!buf || CheckBuffer(buf, len, PROT_READ | PROT_WRITE)) {
        TracePrintf(10, "TtyRead: invalid buffer address %p\n", (uintptr_t) buf);
        return ERROR;
    }

    // Edge case : buffer length of 0
    if(len == 0)
        return 0;

    // References the appropriate terminal structure
    tm = &term[tty_id];

    // Checks if there is a line available in the input buffer
    if(tm->lines > 0) {

        // Copies the data over from the first line
        num_chars = tm->input_bufs[0].size;
        strncpy(buf, tm->input_bufs[0].data, num_chars);

        // Removes the first line
        free(tm->input_bufs[0].data);
        for(i = 1; i < tm->lines; i++)
            tm->input_bufs[i - 1] = tm->input_bufs[i];
        tm->input_bufs = (struct buffer*) realloc(tm->input_bufs, sizeof(struct buffer) * (tm->lines - 1));

        // Decrements the number of lines available in the buffer
        tm->lines--;

        // Returns the number of characters read
        return num_chars;
    }

    // Gets a new process to make active, and switches to it
    new_process = MoveProcesses(READING, tm->read_procs);
    ContextSwitch(Switch, &active->ctx, (void*) active, (void*) new_process);

    // Copies over the contents of the input buffer of the PCB
    strncpy(buf, active->input_buf.data, active->input_buf.size);

    // Returns the number of characters read
    return active->input_buf.size;
}


/* Writes data to a terminal */
int KernelTtyWrite (int tty_id, void *buf, int len) {
    TracePrintf(10, "process %d: executing TtyWrite()\n", active->pid);

    struct terminal *tm;
    struct pcb *new_process;

    // Validates the arguments
    if(tty_id < 0 || tty_id >= NUM_TERMINALS) {
        TracePrintf(10, "TtyWrite: invalid terminal id %d\n", tty_id);
        return ERROR;
    }
    if(len < 0 || len > TERMINAL_MAX_LINE) {
        TracePrintf(10, "TtyWrite: invalid buffer length %d\n", len);
        return ERROR;
    }
    if(!buf || CheckBuffer(buf, len, PROT_READ | PROT_WRITE)) {
        TracePrintf(10, "TtyWrite: invalid buffer address %p\n", (uintptr_t) buf);
        return ERROR;
    }

    // Edge case : buffer length of 0
    if(len == 0)
        return 0;

    // References the appropriate terminal structure
    tm = &term[tty_id];

    // Copies the contents of the buffer to the output buffer of the PCB
    strncpy(active->output_buf.data, buf, len);
    active->output_buf.size = len;
    // Gets a new process to make active
    new_process = MoveProcesses(WRITING, tm->write_procs);

    // Begins transmitting the data if the terminal is available
    if(tm->term_state == FREE) {
        tm->term_state = BUSY;
        TtyTransmit(tty_id, active->output_buf.data, len);
    }

    // Switches to the new process
    ContextSwitch(Switch, &active->ctx, (void*) active, (void*) new_process);

    // Returns the number of characters written
    return len;
}