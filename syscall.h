#ifndef SYSCALL_H
#define SYSCALL_H

#include "kernel.h"
extern struct pcb active;

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