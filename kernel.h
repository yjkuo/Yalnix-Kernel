#ifndef KERNEL_H
#define KERNEL_H


#include <stdint.h>

#include <comp421/hardware.h>
#include <comp421/yalnix.h>

#include "interrupt.h"
#include "queue.h"
#include "list.h"
#include "io.h"


// States a process may be in
enum state_t {RUNNING, READY, DELAYED, READING, WRITING, WAITING, TERMINATED};


// Structure of a PCB
struct pcb {
    unsigned int pid;
    enum state_t state;
    uintptr_t ptaddr0;
    int used_npg;
    uintptr_t sp;
    uintptr_t brk;
    SavedContext ctx;
    int clock_ticks;
    struct buffer input_buf;
    struct buffer output_buf;
    struct pcb *parent;
    struct list *running_chd;
    struct queue *exited_chd;
    int exit_status;
};

// Structure of a PCB frame
struct pcb_frame {
    struct pcb *proc;
    struct pcb_frame *next;
};


// Stores the interrupt vector table
ivt_entry_t ivt[TRAP_VECTOR_SIZE];

// Manages a list of free pages
unsigned int free_head;
unsigned int free_npg;

// Manages a heap of free page tables
int *free_tables;
unsigned int free_size;

// Flag to indicate if virtual memory is enabled
int vm_enabled;

// Stores the address of the region 1 page table
uintptr_t ptaddr1;
struct pte *pt1;

// Stores details of the 'idle' and 'init' processes
struct pcb idle_pcb;
struct pcb *init_pcb;

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
void InitProcess (struct pcb* , enum state_t , uintptr_t );
struct pcb* MoveProcesses (enum state_t , void* );
void RemoveProcess (struct pcb* );
SavedContext* InitContext (SavedContext* , void* , void* );
SavedContext* Switch (SavedContext* , void* , void* );
int LoadProgram (char* , char** , ExceptionInfo* );


#endif