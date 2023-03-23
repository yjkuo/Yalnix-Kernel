#ifndef KERNEL_H
#define KERNEL_H


#include <stdint.h>

#include <comp421/hardware.h>
#include <comp421/yalnix.h>

#include "interrupt.h"


// States a process may be in
typedef enum state_t {RUNNING, READY, BLOCKED} state_t;


// Structure of a PCB
struct pcb {
    unsigned int pid;
    state_t state;
    uintptr_t ptaddr0;
    uintptr_t brk;
    SavedContext ctx;
    int clock_ticks;
    struct pcb *next;
};


// Stores the interrupt vector table
ivt_entry_t ivt[TRAP_VECTOR_SIZE];

// Manages a list of free pages
unsigned int free_head;
unsigned int free_npg;

// Flag to indicate if virtual memory is enabled
int vm_enabled;

// Stores the address of the region 1 page table
uintptr_t ptaddr1;
struct pte *pt1;

// Stores details of the 'idle' and 'init' processes
struct pcb idle_pcb;
struct pcb init_pcb;

// Keeps track of whether the first context switch has been completed
int first_return;

// Stores the address and index of the borrowed PTE
void *borrowed_addr;
int borrowed_index;

// Keeps track of the last assigned PID
unsigned int lastpid;

// Stores the current kernel break address
uintptr_t kernelbrk;

// Manages the active process
struct pcb *active;

// Keeps track of the time quantum for the current process
unsigned int quantum;


// Function prototypes
int GetPage ();
void FreePage (int , int );
void BorrowPTE ();
void ReleasePTE ();
int NewPageTable (uintptr_t );
void CopyKernelStack (uintptr_t );
SavedContext* InitContext (SavedContext* , void* , void* );
SavedContext* Switch (SavedContext* , void* , void* );
int LoadProgram (char* , char** , ExceptionInfo* );


#endif