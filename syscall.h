#ifndef SYSCALL_H
#define SYSCALL_H


#include "kernel.h"


// PCB of currently active process
extern struct pcb *active;


// System call handler prototypes
int KernelFork();
int KernelExec();
void KernelExit();
int KernelWait();
int KernelGetPid();
int KernelBrk();
int KernelDelay();
int KernelTtyRead();
int KernelTtyWrite();


#endif