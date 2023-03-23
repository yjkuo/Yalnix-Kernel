#ifndef LIST_H
#define LIST_H

#include "kernel.h"
#include "queue.h"


// Structure of a process list
typedef struct pcb* list;

// Manages the blocked processes
list blocked;


// Function prototypes
void linit (list* );
void insertl (list* , struct pcb* );
void deletel (list* , struct pcb* );
void clockl (list* );
struct pcb* readyl (list* );
int lempty (list );


#endif