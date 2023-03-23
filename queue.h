#ifndef QUEUE_H
#define QUEUE_H


#include "kernel.h"


// Structure of a process queue
struct queue {
    struct pcb *head;
    struct pcb *tail;
};

// Manages the ready processes
struct queue ready;


// Function prototypes
void qinit (struct queue* );
void enq (struct queue* , struct pcb* );
struct pcb* deq (struct queue* );
int qempty (struct queue* );


#endif