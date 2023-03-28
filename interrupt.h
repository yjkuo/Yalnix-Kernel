#ifndef INTERRUPT_H
#define INTERRUPT_H


#include <comp421/hardware.h>


typedef void (*ivt_entry_t)(ExceptionInfo* );

void KernelHandler (ExceptionInfo* );
void ClockHandler (ExceptionInfo* );
void IllegalHandler (ExceptionInfo* );
void MemoryHandler (ExceptionInfo* );
void MathHandler (ExceptionInfo* );
void TtyReceiveHandler (ExceptionInfo* );
void TtyTransmitHandler (ExceptionInfo* );


#endif