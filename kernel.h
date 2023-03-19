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
    uintptr_t brk;
    unsigned int pfn0;
    int clock_ticks;
    SavedContext ctx;
    struct pcb *next;
};


// // Stores the address of the region 1 page table
// unsigned int pfn1;
// struct pte *pt1;

// Stores the address and index of the borrowed PTE
void *borrowed_addr;
int borrowed_index;

// Manages a list of free pages
// static unsigned int free_head;
unsigned int free_npg;

// // Keeps track of the last assigned PID
// static unsigned int lastpid;

// // Stores the current kernel break address
// static uintptr_t kernelbrk;

// Manages the active process
struct pcb *active;
struct pcb *idle_pcb;
// // Flag of vitual memory enabled
// static int vm_enabled;

// Function prototypes
int GetPage ();
void FreePage (int , int );
void CopyKernelStack(int , int );
int LoadProgram (char* , char** , ExceptionInfo * , struct pcb* );
int ExecuteProgram(ExceptionInfo *);
SavedContext* InitContext (SavedContext* , void* , void* );
SavedContext* Switch (SavedContext* , void* , void* );


#endif