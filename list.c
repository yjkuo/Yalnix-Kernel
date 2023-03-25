#include <stdlib.h>
#include "list.h"


/* Initializes a process list */
void linit (list *l) {
	*l = NULL;
}

/* Adds a process to the list */
void insertl (list *l, struct pcb *p) {
	p->next = *l;
	*l = p;
}

/* Removes a process from the list */
void deletel (list *l, struct pcb *p) {
	struct pcb *ptr = *l;
	if(ptr == p)
		*l = ptr->next;
	else {
		while(ptr->next != p)
			ptr = ptr->next;
		ptr->next = ptr->next->next;
	}
}

/* Updates the remaining clock ticks for all processes in a list */
void clockl (list *l) {
	struct pcb *ptr = *l;
	while(ptr) {
		if(ptr->clock_ticks > 0)
			ptr->clock_ticks--;
		ptr = ptr->next;
	}
}

/* Updates the parent pointer for all processes in a list */
void exitl (list *l) {
	struct pcb *ptr = *l;
	while(ptr) {
		ptr->parent = NULL;
		ptr = ptr->next;
	}
}

/* Returns a process that is done blocking */
struct pcb* readyl (list *l) {
	struct pcb *ptr = *l;
	while(ptr) {
		if(ptr->clock_ticks == 0) {
			ptr->clock_ticks = -1;
			return ptr;
		}
		ptr = ptr->next;
	}
	return NULL;
}

/* Checks if a list is empty */
int lempty (list l) {
	return (l == NULL);
}