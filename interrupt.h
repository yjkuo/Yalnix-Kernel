#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <comp421/hardware.h>

typedef void (*ivt_entry_t)(ExceptionInfo *);

extern void ker_handler(ExceptionInfo *);
extern void clk_handler(ExceptionInfo *);
extern void ill_handler(ExceptionInfo *);
extern void mem_handler(ExceptionInfo *);
extern void math_handler(ExceptionInfo *);
extern void tty_tx_handler(ExceptionInfo *);
extern void tty_recv_handler(ExceptionInfo *);

#endif