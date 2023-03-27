#include <stdlib.h>
#include <string.h>

#include <comp421/yalnix.h>

#include "syscall.h"
#include "kernel.h"
#include "queue.h"
#include "list.h"
#include "io.h"


/* Creates a new process */
int KernelFork (int caller_pid) {
    TracePrintf(10, "process %d: executing Fork()\n", active->pid);

    // Creates a PCB for the child process
    struct pcb *child_process = (struct pcb*) malloc(sizeof(struct pcb));
    InitProcess(child_process, READY, NewPageTable(active->ptaddr0));

    // Sets the program break for the child process
    child_process->brk = active->brk;

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
        return child_process->pid;

    // Case 2 : child process
    else {
        WriteRegister(REG_PTR0, (RCS421RegVal) child_process->ptaddr0);
        WriteRegister(REG_TLB_FLUSH, TLB_FLUSH_0);
        return 0;
    }
}


/* Replaces the currently running program in the calling process’s memory */
int KernelExec (char *filename, char **argvec, ExceptionInfo *info) {
    TracePrintf(10, "process %d: executing Exec()\n", active->pid);

    // Loads the specified program
    LoadProgram(filename, argvec, info);

    // Returns an error if the previous call returns
    return ERROR;
}


/* Terminates the current process */
void KernelExit (int status) {
    TracePrintf(10, "process %d: executing Exit()\n", active->pid);

    struct pcb *new_process;

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
            enq(&ready, active->parent);
        }
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

    // Checks the validity of the arguments
    if(!status_ptr) {
        TracePrintf(10, "Wait: invalid status pointer 0x%x\n", (uintptr_t) status_ptr);
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

    // Checks if the specified break address is valid
    if(!addr || (uintptr_t) addr < 0 || (uintptr_t) addr >= DOWN_TO_PAGE(sp) - PAGESIZE) {
        TracePrintf(10, "Brk: invalid address 0x%x\n", (uintptr_t) addr);
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

    struct pcb *new_process;

    // Checks for an invalid number of clock ticks
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

    struct pcb *new_process;
    int num_chars;
    char temp_buffer[TERMINAL_MAX_LINE];

    // Checks the validity of the arguments
    if(tty_id < 0 || tty_id >= NUM_TERMINALS) {
        TracePrintf(10, "TtyRead: invalid terminal id %d\n", tty_id);
        return ERROR;
    }
    if(!buf) {
        TracePrintf(10, "TtyRead: invalid buffer address 0x%x\n", (uintptr_t) buf);
        return ERROR;
    }
    if(len < 0 || len > TERMINAL_MAX_LINE) {
        TracePrintf(10, "TtyRead: invalid buffer length %d\n", len);
        return ERROR;
    }

    // Edge case : buffer length of 0
    if(len == 0)
        return 0;

    // Checks if there is a line available in the input buffer
    if(term[tty_id].lines > 0) {

        // Finds the number of characters in the line
        for(num_chars = 0; num_chars < len; )
            if(term[tty_id].input_buf.data[num_chars++] == '\n')
                break;

        // Copies the data over from the input buffer
        strncpy(buf, term[tty_id].input_buf.data, num_chars);

        // Updates the input buffer
        term[tty_id].input_buf.size -= num_chars;
        strncpy(temp_buffer, (term[tty_id].input_buf.data + num_chars), term[tty_id].input_buf.size);
        strncpy(term[tty_id].input_buf.data, temp_buffer, term[tty_id].input_buf.size);
        term[tty_id].lines--;

        // Returns the number of characters read
        return num_chars;
    }

    // Gets a new process to make active, and switches to it
    new_process = MoveProcesses(READING, term[tty_id].read_procs);
    ContextSwitch(Switch, &active->ctx, (void*) active, (void*) new_process);

    // Copies over the contents of the input buffer of the PCB
    strncpy(buf, active->input_buf.data, active->input_buf.size);

    // Returns the number of characters read
    return active->input_buf.size;
}


/* Writes data to a terminal */
int KernelTtyWrite (int tty_id, void *buf, int len) {
    TracePrintf(10, "process %d: executing TtyWrite()\n", active->pid);

    struct pcb *new_process;

    // Checks the validity of the arguments
    if(tty_id < 0 || tty_id >= NUM_TERMINALS) {
        TracePrintf(10, "TtyWrite: invalid terminal id %d\n", tty_id);
        return ERROR;
    }
    if(!buf) {
        TracePrintf(10, "TtyWrite: invalid buffer address 0x%x\n", (uintptr_t) buf);
        return ERROR;
    }
    if(len < 0 || len > TERMINAL_MAX_LINE) {
        TracePrintf(10, "TtyWrite: invalid buffer length %d\n", len);
        return ERROR;
    }

    // Edge case : buffer length of 0
    if(len == 0)
        return 0;

    // Copies the contents of the buffer to the output buffer of the PCB
    strncpy(active->output_buf.data, buf, len);
    active->output_buf.size = len;

    // Gets a new process to make active
    new_process = MoveProcesses(WRITING, term[tty_id].write_procs);

    // Begins transmitting the data if the terminal is available
    if(term[tty_id].term_state == FREE) {
        term[tty_id].term_state = BUSY;
        TtyTransmit(tty_id, active->output_buf.data, len);
    }

    // Switches to the new process
    ContextSwitch(Switch, &active->ctx, (void*) active, (void*) new_process);

    // Returns the number of characters written
    return len;
}