#include <comp421/yalnix.h>
#include "interrupt.h"
#include "syscall.h"
#include "queue.h"

void kerHandler(ExceptionInfo *info) {
    TracePrintf(1, "kernel interrupt\n");
    switch (info->code) {
        // case YALNIX_EXEC:
        //     KernelExec();
        //     break;
        // case YALNIX_FORK:
        //     KernelFork();
        //     break;		
        // case YALNIX_EXIT:
        //     KernelExit();
        //     break;
        // case YALNIX_WAIT:
        //     KernelWait();
        //     break;
        case YALNIX_GETPID:
            info->regs[0] = KernelGetPid();
            break;
        case YALNIX_BRK:
            info->regs[0] = KernelBrk((void *)info->regs[1], info->sp);
            break;
        case YALNIX_DELAY:
            info->regs[0] = KernelDelay(info->regs[1]);
            break;
        // case YALNIX_TTY_READ:
        //     KernelTtyRead();
        //     break;
        // case YALNIX_TTY_WRITE
        //     KernelTtyWrite();
        //     break;
    }
    // Halt();
}

void clkHandler(ExceptionInfo *info) {
    TracePrintf(1, "clock interrupt\n");
    if (!qempty(&blocked)) {
        struct pcb *cur = blocked.head;
        while (cur) {
            cur->clock_ticks--;
            TracePrintf(0, "clock %d\n", cur->clock_ticks);
            if (cur->clock_ticks == 0) {
                ContextSwitch(Switch, &active->ctx, (void *) active, (void *) cur);
                // cur->state = READY;
                // // need to modify deq to specify which process to deq
                // deq(&blocked);
                // enq(&ready, cur);
            }
            cur = cur->next;
        }
    }
}

void illHandler(ExceptionInfo *info) {
    TracePrintf(1, "illigal interrupt\n");
    Halt();
}

void memHandler(ExceptionInfo *info) {
    TracePrintf(1, "memory Interrupt %d\n", info->vector);
    Halt();
}

void mathHandler(ExceptionInfo *info) {
    TracePrintf(1, "math interrupt\n");
    Halt();
}

void ttyXmitHandler(ExceptionInfo *info) {
    TracePrintf(1, "tty transmit interrupt\n");
    Halt();
}

void ttyRecvHandler(ExceptionInfo *info) {
    TracePrintf(1, "tty receive interrupt\n");
    Halt();
}