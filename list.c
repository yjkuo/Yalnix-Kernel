#include <stdlib.h>

#include "list.h"


/* Initializes a process list */
void linit (struct list *l) {

	// Marks the list as empty
	l->head = NULL;
	l->size = 0;
}


/* Adds a process to the list */
void insertl (struct list *l, struct pcb *p) {

	struct pcb_frame *pframe;

	// Creates a new process frame
	pframe = (struct pcb_frame*) malloc(sizeof(struct pcb_frame));
	pframe->proc = p;

	// Adds it at the start of the list
	pframe->next = l->head;
	l->head = pframe;

	// Increments the size of the list
	l->size++;
}


/* Removes a process from the list */
void deletel (struct list *l, struct pcb *p) {

	struct pcb_frame *pframe = l->head, *delete_pframe = NULL;

	// Case 1 : first frame needs to be deleted
	if(pframe->proc == p) {
		delete_pframe = pframe;

		// Updates the list head
		l->head = pframe->next;
	}

	// Case 2 : any other frame needs to be deleted
	else {

		// Finds the right frame to delete
		while(pframe->next->proc != p)
			pframe = pframe->next;
		delete_pframe = pframe->next;

		// Updates the frame pointers
		pframe->next = pframe->next->next;
	}

	// Frees the process frame
	free(delete_pframe);

	// Decrements the size of the list
	l->size--;
}


/* Updates the remaining clock ticks for all processes in a list */
void clockl (struct list *l) {

	struct pcb_frame *pframe;

	// Iterates over each process in the list
	pframe = l->head;
	while(pframe) {

		// Decrements the clock ticks if applicable
		if(pframe->proc->clock_ticks > 0)
			pframe->proc->clock_ticks--;

		pframe = pframe->next;
	}
}


/* Updates the parent process for all processes in a list */
void exitl (struct list *l) {

	struct pcb_frame *pframe;

	// Iterates over each process in the list
	pframe = l->head;
	while(pframe) {

		// Sets the parent pointer to NULL
		pframe->proc->parent = NULL;

		pframe = pframe->next;
	}
}


/* Returns a process that is done blocking */
struct pcb* readyl (struct list *l) {

	struct pcb_frame *pframe;

	// Iterates over each process in the list
	pframe = l->head;
	while(pframe) {

		// Checks if the clock ticks for a process are up
		if(pframe->proc->clock_ticks == 0) {
			pframe->proc->clock_ticks = -1;
			return pframe->proc;
		}

		pframe = pframe->next;
	}

	// Returns NULL if there are no such processes
	return NULL;
}


/* Checks if a list is empty */
int lempty (struct list l) {
	return (l.size == 0);
}


/* Destroys a process list */
void ldestroy (struct list *l) {

	struct pcb_frame *pframe, *delete_pframe;

	// Iterates over each process in the list
	pframe = l->head;
	while(pframe) {

		// Gets the frame to be deleted
		delete_pframe = pframe;
		pframe = pframe->next;

		// Frees the process frame
		free(delete_pframe);
	}
}