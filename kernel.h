#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include "interrupt.h"

typedef enum state_t { READY, RUNNING, BLOCKED} state_t;

struct pcb {
    unsigned int pid;
    state_t state;
    SavedContext *ctxp;
    uintptr_t pageTable;
    uintptr_t brk;
    void *ptPhysical;
    ExceptionInfo info;
    struct pcb *next;
};

int getPage();
void freePage(int, int);
int LoadProgram(char *, char **, struct pcb *);
int ExecuteProgram(ExceptionInfo *);
#endif