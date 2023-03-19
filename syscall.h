#ifndef SYSCALL_H
#define SYSCALL_H


#include "kernel.h"




// PCB of currently active process
extern struct pcb *active;
extern struct pcb *idle_pcb;
extern struct queue ready;
extern struct queue blocked;
extern unsigned int free_npg;
extern int GetPage();
extern void FreePage(int , int);
extern SavedContext* Switch (SavedContext* , void* , void* );

// System call handler prototypes
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