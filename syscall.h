#ifndef SYSCALL_H
#define SYSCALL_H


#include "kernel.h"


// System call handler prototypes
int KernelFork(int );
int KernelExec(char* , char** , ExceptionInfo* );
void KernelExit(int );
int KernelWait();
int KernelGetPid();
int KernelBrk(void *addr, void *sp);
int KernelDelay(int);
int KernelTtyRead();
int KernelTtyWrite();


#endif