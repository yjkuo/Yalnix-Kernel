#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include "interrupt.h"

typedef enum state_t { READY, RUNNING, BLOCKED} state_t;

struct pcb {
    unsigned int pid;
    state_t state;
    SavedContext *ctxp;
    struct pcb *next;
};

int freePageHead;
int freePageCount = 0;
ivt_entry_t *ivt;
unsigned int last_pid = 0;
uintptr_t kernelBrk;

int getPage();
void freePage(int);
int LoadProgram(char *name, char **args, ExceptionInfo *info);
#endif