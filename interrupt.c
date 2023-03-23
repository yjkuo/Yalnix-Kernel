#include <stdio.h>
#include <stdlib.h>

#include <comp421/yalnix.h>
#include "interrupt.h"
#include "syscall.h"
#include "queue.h"

#include "interrupt.h"
#include "syscall.h"
#include "queue.h"
#include "list.h"


/* Handles a system call */
void KernelHandler (ExceptionInfo *info) {
    TracePrintf(5, "kernel interrupt %d\n", info->code);

    // Selects the appropriate handler function
    switch(info->code) {

        // Handles GetPid()
        case YALNIX_GETPID :
            info->regs[0] = KernelGetPid();
            break;

        // Handles Brk()
        case YALNIX_BRK :
            info->regs[0] = KernelBrk((void*) info->regs[1], info->sp);
            break;

        // Handles Delay()
        case YALNIX_DELAY :
            info->regs[0] = KernelDelay(info->regs[1]);
            break;
    }
}


/* Handles a clock interrupt */
void ClockHandler (ExceptionInfo *info) {
    TracePrintf(5, "clock interrupt\n");

    // Updates clock ticks for all processes in the blocked list
    clockl(&blocked);

    // Possibly moves processes from the blocked queue to the ready queue
    struct pcb *ready_process;
    while((ready_process = readyl(&blocked))) {
        ready_process->state = READY;
        deletel(&blocked, ready_process);
        enq(&ready, ready_process);
    }

    // Checks if the time quantum for the process is up
    quantum = ~quantum;
    if(!quantum) {

        // Checks if the ready queue has a process
        if(qempty(&ready))
            return;

        // Marks the current process as ready
        active->state = READY;

        // Gets a process from the ready queue
        struct pcb *new_process = deq(&ready);

        // Switches to the new process
        ContextSwitch(Switch, &active->ctxp, (void*) active, (void*) new_process);
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

    Halt();
}


void MemoryHandler (ExceptionInfo *info) {
    TracePrintf(5, "memory Interrupt %d\n", info->vector);
    Halt();
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

    Halt();
}


void TtyTransmitHandler (ExceptionInfo *info) {
    TracePrintf(5, "tty transmit interrupt\n");
    Halt();
}

void TtyReceiveHandler (ExceptionInfo *info) {
    TracePrintf(5, "tty receive interrupt\n");
    Halt();
}