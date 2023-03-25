#ifndef LIST_H
#define LIST_H


// #include "queue.h"


// Structure of a process list
typedef struct pcb* list;
#include "kernel.h"

// Manages the blocked processes
list blocked;


// Function prototypes
void linit (list* );
void insertl (list* , struct pcb* );
void deletel (list* , struct pcb* );
void clockl (list* );
void exitl (list* );
struct pcb* readyl (list* );
int lempty (list );


#endif