#ifndef KERNEL_H
#define KERNEL_H


#include <stdint.h>

#include "interrupt.h"


// Stores the interrupt vector table
ivt_entry_t ivt[TRAP_VECTOR_SIZE];

// Flag to indicate if virtual memory is enabled
int vm_enabled;

// Keeps track of whether the first context switch has been completed
int first_return;

// Stores the current kernel break address
uintptr_t kernelbrk;


#endif