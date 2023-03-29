#ifndef PROC_H
#define PROC_H


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
    uintptr_t user_stack_base;
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

// Stores details of the 'idle' and 'init' processes
struct pcb idle_pcb;
struct pcb *init_pcb;

// Keeps track of the last assigned PID
unsigned int lastpid;

// Manages the active process
struct pcb *active;

// Keeps track of the time quantum for the current process
unsigned int quantum;


// Function prototypes
int InitProcess (struct pcb* , enum state_t , uintptr_t );
struct pcb* MoveProcesses (enum state_t , void* );
void RemoveProcess (struct pcb* );
SavedContext* InitContext (SavedContext* , void* , void* );
SavedContext* Switch (SavedContext* , void* , void* );
int LoadProgram (char* , char** , ExceptionInfo* );


#endif