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

static int freePageHead;
static int freePageCount = 0;
static unsigned int lastPid = 0;
static uintptr_t kernelBrk;

struct pcb active;
static struct pcb readyHead;
static struct pcb blockedHead;

int getPage();
void freePage(int, int);
int LoadProgram(char *, char **, ExceptionInfo *);

#endif