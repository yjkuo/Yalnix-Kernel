#ifndef QUEUE_H
#define QUEUE_H




// Structure of a process queue
struct queue {
    struct pcb *head;
    struct pcb *tail;
};

#include "kernel.h"
// Manages the ready processes
struct queue ready;


// Function prototypes
void qinit (struct queue* );
void enq (struct queue* , struct pcb* );
struct pcb* deq (struct queue* );
int qempty (struct queue* );


#endif