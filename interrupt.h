#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <comp421/hardware.h>

typedef void (*ivt_entry_t)(ExceptionInfo *);

void kerHandler(ExceptionInfo *);
void clkHandler(ExceptionInfo *);
void illHandler(ExceptionInfo *);
void memHandler(ExceptionInfo *);
void mathHandler(ExceptionInfo *);
void ttyXmitHandler(ExceptionInfo *);
void ttyRecvHandler(ExceptionInfo *);

#endif