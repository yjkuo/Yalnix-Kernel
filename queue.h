#ifndef QUEUE_H
#define QUEUE_H




// Declares structures to allow PCB manipulation
struct pcb;
struct pcb_frame;


// Structure of a process queue
struct queue {
    struct pcb_frame *head;
    struct pcb_frame *tail;
    int size;
};

#include "kernel.h"
// Manages the ready processes
struct queue ready;


// Function prototypes
void qinit (struct queue* );
void enq (struct queue* , struct pcb* );
struct pcb* deq (struct queue* );
struct pcb* peekq (struct queue );
int qempty (struct queue );
void qdestroy (struct queue* );


#endif