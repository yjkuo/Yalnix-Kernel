#ifndef SYSCALL_H
#define SYSCALL_H


#include "kernel.h"


// System call handler prototypes
int KernelFork ();
int KernelExec ();
void KernelExit ();
int KernelWait ();
int KernelGetPid ();
int KernelBrk (void* , void* );
int KernelDelay (int );
int KernelTtyRead ();
int KernelTtyWrite ();


#endif