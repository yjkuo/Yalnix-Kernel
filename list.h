#ifndef LIST_H
#define LIST_H


#include "queue.h"
#include "proc.h"


// Declares structures to allow PCB manipulation
struct pcb;
struct pcb_frame;


// Structure of a process list
struct list {
    struct pcb_frame *head;
    int size;
};

// Manages the blocked processes
struct list blocked;


// Function prototypes
void linit (struct list* );
void insertl (struct list* , struct pcb* );
void deletel (struct list* , struct pcb* );
void clockl (struct list* );
void exitl (struct list* );
struct pcb* readyl (struct list* );
int lempty (struct list );
void ldestroy (struct list* );


#endif