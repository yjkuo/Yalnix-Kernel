#include <stdio.h>
#include <stdlib.h>

#include <comp421/yalnix.h>

#include "interrupt.h"
#include "syscall.h"
#include "queue.h"
#include "list.h"
#include "io.h"


/* Handles a system call */
void KernelHandler (ExceptionInfo *info) {
    TracePrintf(5, "kernel interrupt %d\n", info->code);

    // Selects the appropriate handler function
    switch(info->code) {

        // Handles Fork()
        case YALNIX_FORK :
            info->regs[0] = KernelFork((int) active->pid);
            break;

        // Handles Exec()
        case YALNIX_EXEC :
            info->regs[0] = KernelExec((char*) info->regs[1], (char**) info->regs[2], info);
            break;

        // Handles Exit()
        case YALNIX_EXIT:
            KernelExit((int) info->regs[1]);
            break;

        // Handles Wait()
        case YALNIX_WAIT:
            info->regs[0] = KernelWait((int*) info->regs[1]);
            break;
    
        // Handles GetPid()
        case YALNIX_GETPID :
            info->regs[0] = KernelGetPid();
            break;

        // Handles Brk()
        case YALNIX_BRK :
            info->regs[0] = KernelBrk((void*) info->regs[1]);
            break;

        // Handles Delay()
        case YALNIX_DELAY :
            info->regs[0] = KernelDelay((int) info->regs[1]);
            break;

        // Handles TtyRead()
        case YALNIX_TTY_READ :
            info->regs[0] = KernelTtyRead((int) info->regs[1], (void*) info->regs[2], (int) info->regs[3]);
            break;

        // Handles TtyWrite()
        case YALNIX_TTY_WRITE :
            info->regs[0] = KernelTtyWrite((int) info->regs[1], (void*) info->regs[2], (int) info->regs[3]);
            break;
    }
}


/* Handles a clock interrupt */
void ClockHandler (ExceptionInfo *info) {
    TracePrintf(5, "clock interrupt\n");

    struct pcb *ready_process, *new_process;

    // Updates clock ticks for all processes in the blocked list
    clockl(&blocked);

    // Moves processes from the blocked queue to the ready queue
    while((ready_process = readyl(&blocked))) {
        ready_process->state = READY;
        deletel(&blocked, ready_process);
        enq(&ready, ready_process);
    }

    // Checks if the time quantum for the process is up
    quantum++;
    if(quantum >= 2) {

        // Checks if the ready queue has a process
        if(qempty(ready))
            return;

        // Resets the time quantum for the new process
        quantum = 0;

        // Gets a new process to make active
        new_process = MoveProcesses(READY, &ready);

        // Switches to the new process
        ContextSwitch(Switch, &active->ctx, (void*) active, (void*) new_process);
    }
}


/* Handles an illegal instruction interrupt */
void IllegalHandler (ExceptionInfo *info) {
    TracePrintf(5, "illegal instruction exception\n");

    // Prints an appropriate error message
    switch(info->code) {

        case TRAP_ILLEGAL_ILLOPC :
            fprintf(stderr, "process %d: illegal opcode\n", active->pid);
            TracePrintf(5, "process %d: illegal opcode\n", active->pid);
            break;

        case TRAP_ILLEGAL_ILLOPN :
            fprintf(stderr, "process %d: illegal operand\n", active->pid);
            TracePrintf(5, "process %d: illegal operand\n", active->pid);
            break;

        case TRAP_ILLEGAL_ILLADR :
            fprintf(stderr, "process %d: illegal addressing mode\n", active->pid);
            TracePrintf(5, "process %d: illegal addressing mode\n", active->pid);
            break;

        case TRAP_ILLEGAL_ILLTRP :
            fprintf(stderr, "process %d: illegal software trap\n", active->pid);
            TracePrintf(5, "process %d: illegal software trap\n", active->pid);
            break;

        case TRAP_ILLEGAL_PRVOPC :
            fprintf(stderr, "process %d: privileged opcode\n", active->pid);
            TracePrintf(5, "process %d: privileged opcode\n", active->pid);
            break;

        case TRAP_ILLEGAL_PRVREG :
            fprintf(stderr, "process %d: privileged register\n", active->pid);
            TracePrintf(5, "process %d: privileged register\n", active->pid);
            break;

        case TRAP_ILLEGAL_COPROC :
            fprintf(stderr, "process %d: coprocessor error\n", active->pid);
            TracePrintf(5, "process %d: coprocessor error\n", active->pid);
            break;

        case TRAP_ILLEGAL_BADSTK :
            fprintf(stderr, "process %d: bad stack\n", active->pid);
            TracePrintf(5, "process %d: bad stack\n", active->pid);
            break;

        case TRAP_ILLEGAL_KERNELI :
            fprintf(stderr, "process %d: Linux kernel sent SIGILL\n", active->pid);
            TracePrintf(5, "process %d: Linux kernel sent SIGILL\n", active->pid);
            break;

        case TRAP_ILLEGAL_KERNELB :
            fprintf(stderr, "process %d: Linux kernel sent SIGBUS\n", active->pid);
            TracePrintf(5, "process %d: Linux kernel sent SIGBUS\n", active->pid);
            break;

        case TRAP_ILLEGAL_USERIB :
            fprintf(stderr, "process %d: received SIGILL or SIGBUS from user\n", active->pid);
            TracePrintf(5, "process %d: received SIGILL or SIGBUS from user\n", active->pid);
            break;

        case TRAP_ILLEGAL_ADRALN :
            fprintf(stderr, "process %d: invalid address alignment\n", active->pid);
            TracePrintf(5, "process %d: invalid address alignment\n", active->pid);
            break;

        case TRAP_ILLEGAL_ADRERR :
            fprintf(stderr, "process %d: non-existent physical address\n", active->pid);
            TracePrintf(5, "process %d: non-existent physical address\n", active->pid);
            break;

        case TRAP_ILLEGAL_OBJERR :
            fprintf(stderr, "process %d: object-specific HW error\n", active->pid);
            TracePrintf(5, "process %d: object-specific HW error\n", active->pid);
            break;
    }

    // Terminates the process with an error
    KernelExit(ERROR);
}


/* Handles a memory trap */
void MemoryHandler (ExceptionInfo *info) {
    TracePrintf(5, "memory trap\n");

    uintptr_t trap_addr;
    struct pte *pt0;
    int start_index, end_index;
    int i;

    // Finds the memory address that caused the exception
    trap_addr = (uintptr_t) info->addr;

    // Checks if the process is attempting to grow its user stack
    if(trap_addr >= UP_TO_PAGE(active->brk) + PAGESIZE && trap_addr < active->user_stack_base) {
        TracePrintf(5, "process %d: growing user stack\n", active->pid);

        // Finds the range of pages to be allocated
        start_index = (DOWN_TO_PAGE(trap_addr) - VMEM_0_BASE) >> PAGESHIFT;
        end_index = (active->user_stack_base - VMEM_0_BASE) >> PAGESHIFT;

        // Checks if there are enough free pages
        if(end_index - start_index > free_npg) {
            TracePrintf(5, "process %d: not enough free pages\n", active->pid);
            KernelExit(ERROR);
        }
    
        // Accesses the region 0 page table of the current process
        BorrowPTE();
        pt1[borrowed_index].pfn = (active->ptaddr0 - PMEM_BASE) >> PAGESHIFT;
        pt0 = (struct pte*) (borrowed_addr + ((active->ptaddr0 - PMEM_BASE) & PAGEOFFSET));

        // Gets these new pages
        for(i = start_index; i < end_index; i++) {
            pt0[i].valid = 1;
            pt0[i].pfn = GetPage();
            pt0[i].kprot = PROT_READ | PROT_WRITE;
            pt0[i].uprot = PROT_READ | PROT_WRITE;
        }

        // Returns the borrowed PTE and flushes the entry for the region 0 page table
        ReleasePTE();
        WriteRegister(REG_TLB_FLUSH, (RCS421RegVal) pt0);

        // Updates the user stack base
        active->user_stack_base = DOWN_TO_PAGE(trap_addr);
    }

    // Else, prints an appropriate error message
    else {
        switch(info->code) {

            case TRAP_MEMORY_MAPERR :
                fprintf(stderr, "process %d: no mapping at address %p\n", active->pid, info->addr);
                TracePrintf(5, "process %d: no mapping at address %p\n", active->pid, info->addr);
                break;

            case TRAP_MEMORY_ACCERR :
                fprintf(stderr, "process %d: protection violation at address %p\n", active->pid, info->addr);
                TracePrintf(5, "process %d: protection violation at address %p\n", active->pid, info->addr);
                break;

            case TRAP_MEMORY_KERNEL :
                fprintf(stderr, "process %d: Linux kernel sent SIGSEGV at address %p\n", active->pid, info->addr);
                TracePrintf(5, "process %d: Linux kernel sent SIGSEGV at address %p\n", active->pid, info->addr);
                break;

            case TRAP_MEMORY_USER :
                fprintf(stderr, "process %d: received SIGSEGV from user at address %p\n", active->pid, info->addr);
                TracePrintf(5, "process %d: received SIGSEGV from user at address %p\n", active->pid, info->addr);
                break;
        }

        // Terminates the process with an error
        KernelExit(ERROR);
    }
}


/* Handles an arithmetic exception */
void MathHandler (ExceptionInfo *info) {
    TracePrintf(5, "arithmetic exception\n");

    // Prints an appropriate error message
    switch(info->code) {

        case TRAP_MATH_INTDIV :
            fprintf(stderr, "process %d: integer divide by zero\n", active->pid);
            TracePrintf(5, "process %d: integer divide by zero\n", active->pid);
            break;

        case TRAP_MATH_INTOVF :
            fprintf(stderr, "process %d: integer overflow\n", active->pid);
            TracePrintf(5, "process %d: integer overflow\n", active->pid);
            break;

        case TRAP_MATH_FLTDIV :
            fprintf(stderr, "process %d: floating divide by zero\n", active->pid);
            TracePrintf(5, "process %d: floating divide by zero\n", active->pid);
            break;

        case TRAP_MATH_FLTOVF :
            fprintf(stderr, "process %d: floating overflow\n", active->pid);
            TracePrintf(5, "process %d: floating overflow\n", active->pid);
            break;

        case TRAP_MATH_FLTUND :
            fprintf(stderr, "process %d: floating underflow\n", active->pid);
            TracePrintf(5, "process %d: floating underflow\n", active->pid);
            break;

        case TRAP_MATH_FLTRES :
            fprintf(stderr, "process %d: floating inexact result\n", active->pid);
            TracePrintf(5, "process %d: floating inexact result\n", active->pid);
            break;

        case TRAP_MATH_FLTINV :
            fprintf(stderr, "process %d: invalid floating operation\n", active->pid);
            TracePrintf(5, "process %d: invalid floating operation\n", active->pid);
            break;

        case TRAP_MATH_FLTSUB :
            fprintf(stderr, "process %d: FP subscript out of range\n", active->pid);
            TracePrintf(5, "process %d: FP subscript out of range\n", active->pid);
            break;

        case TRAP_MATH_KERNEL :
            fprintf(stderr, "process %d: Linux kernel sent SIGFPE\n", active->pid);
            TracePrintf(5, "process %d: Linux kernel sent SIGFPE\n", active->pid);
            break;

        case TRAP_MATH_USER :
            fprintf(stderr, "process %d: received SIGFPE from user\n", active->pid);
            TracePrintf(5, "process %d: received SIGFPE from user\n", active->pid);
            break;
    }

    // Terminates the process with an error
    KernelExit(ERROR);
}


/* Handles a receive interrupt from the terminal */
void TtyReceiveHandler (ExceptionInfo *info) {
    TracePrintf(5, "tty receive interrupt\n");

    int tty_id;
    struct terminal *tm;
    struct pcb *next_process;
    struct buffer *temp_buffer;
    int len;

    // Gets the terminal for which the interrupt occurred
    tty_id = info->code;
    tm = &term[tty_id];

    // Checks if any processes are waiting in the read queue
    if(!qempty(*tm->read_procs)) {

        // Finds the first process that is waiting
        next_process = deq(tm->read_procs);

        // Retrieves the line from the terminal
        len = TtyReceive(tty_id, next_process->input_buf.data, TERMINAL_MAX_LINE);
        next_process->input_buf.size = len;

        // Unblocks the process
        next_process->state = READY;
        enq(&ready, next_process);
    }

    // Else, stores the line in the kernel's input buffer
    else {

        // Creates space at the end of the buffer for a new line
        temp_buffer = (struct buffer*) realloc(tm->input_bufs, sizeof(struct buffer) * (tm->lines + 1));
        if(!temp_buffer) {
            TracePrintf(5, "cannot buffer any more terminal input\n");
            return;
        }
        tm->input_bufs = temp_buffer;

        // Allocates memory for the line
        tm->input_bufs[tm->lines].data = (char*) malloc(TERMINAL_MAX_LINE);
        if(!tm->input_bufs[tm->lines].data) {
            TracePrintf(5, "cannot buffer any more terminal input\n");
            tm->input_bufs = (struct buffer*) realloc(tm->input_bufs, sizeof(struct buffer) * tm->lines);
            return;
        }

        // Retrieves the line from the terminal
        len = TtyReceive(tty_id, tm->input_bufs[tm->lines].data, TERMINAL_MAX_LINE);
        tm->input_bufs[tm->lines].data = (char*) realloc(tm->input_bufs[tm->lines].data, len);
        tm->input_bufs[tm->lines].size = len;

        // Increments the number of lines available in the buffer
        tm->lines++;
    }
}


/* Handles a transmit interrupt from the terminal */
void TtyTransmitHandler (ExceptionInfo *info) {
    TracePrintf(5, "tty transmit interrupt\n");

    int tty_id;
    struct terminal *tm;
    struct pcb *ready_process, *next_process;

    // Gets the terminal for which the interrupt occurred
    tty_id = info->code;
    tm = &term[tty_id];

    // Unblocks the last process from the write queue for this terminal
    ready_process = deq(tm->write_procs);
    ready_process->state = READY;
    enq(&ready, ready_process);

    // Checks if any other processes are waiting in this queue
    if(!qempty(*tm->write_procs)) {

        // Finds the next process that is waiting
        next_process = peekq(*tm->write_procs);

        // Begins transmitting the data for this process
        TtyTransmit(tty_id, next_process->output_buf.data, next_process->output_buf.size);
    }

    // Else, marks the terminal as ready for output
    else
        tm->term_state = FREE;
}