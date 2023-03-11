#include "syscall.h"
#include "interrupt.h"
#include <comp421/yalnix.h>

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
        // case YALNIX_BRK:
        //     KernelBrk();
        //     break;
        // case YALNIX_DELAY:
        //     KernelDelay();
        //     break;
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
    // Halt();
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