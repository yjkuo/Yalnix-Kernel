#ifndef MMU_H
#define MMU_H


#include <stdint.h>

#include <comp421/hardware.h>


// Manages a list of free pages
unsigned int free_head;
unsigned int free_npg;

// Manages a heap of free page tables
int *free_tables;
unsigned int free_ntbl;

// Stores the address of the region 1 page table
uintptr_t ptaddr1;
struct pte *pt1;

// Stores the address and index of the borrowed PTE
void *borrowed_addr;
int borrowed_index;

// Temporarily buffers borrowed PTEs if required
struct pte pte_buffer[4];
int pte_count;


// Function prototypes
int GetPage ();
void FreePage (int , int );
void BorrowPTE ();
void ReleasePTE ();
uintptr_t GetPageTable ();
int InitPageTable (uintptr_t );
void FreePageTable (uintptr_t );
void CopyKernelStack (uintptr_t );


#endif