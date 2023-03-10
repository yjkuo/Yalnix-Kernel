#include "interrupt.h"

extern void ker_handler(ExceptionInfo *info) {
    TracePrintf(1, "kernel interrupt\n");
    Halt();
}

extern void clk_handler(ExceptionInfo *info) {
    TracePrintf(1, "clock interrupt\n");
    Halt();
}

extern void ill_handler(ExceptionInfo *info) {
    TracePrintf(1, "illigal interrupt\n");
    Halt();
}

extern void mem_handler(ExceptionInfo *info) {
    TracePrintf(1, "mem interrupt\n");
    Halt();
}

extern void math_handler(ExceptionInfo *info) {
    TracePrintf(1, "math interrupt\n");
    Halt();
}

extern void tty_tx_handler(ExceptionInfo *info) {
    TracePrintf(1, "tty transmit interrupt\n");
    Halt();
}

extern void tty_recv_handler(ExceptionInfo *info) {
    TracePrintf(1, "tty receive interrupt\n");
    Halt();
}