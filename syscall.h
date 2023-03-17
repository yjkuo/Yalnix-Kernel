#ifndef SYSCALL_H
#define SYSCALL_H

#include "kernel.h"
extern struct pcb *active;
extern int freePageCount;
extern int getPage();
extern void freePage(int, int);

int KernelFork();
int KernelExec();
void KernelExit();
int KernelWait();
int KernelGetPid();
int KernelBrk(void *addr, void *sp);
int KernelDelay(int);
int KernelTtyRead();
int KernelTtyWrite();

#endif