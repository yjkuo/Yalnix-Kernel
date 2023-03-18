#ifndef QUEUE_H
#define QUEUE_H

#include "kernel.h"


// Structure of a process queue
struct queue {
    struct pcb *head;
    struct pcb *tail;
};

// Manages the ready and blocked processes
static struct queue ready;
static struct queue blocked;


// Function prototypes
void qinit (struct queue* );
void enq (struct queue* , struct pcb* );
struct pcb* deq (struct queue* );
int qempty (struct queue* );


#endif