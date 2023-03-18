#include <stdlib.h>
#include "queue.h"


/* Initializes a process queue */
void qinit (struct queue *q) {
	q->head = NULL;
	q->tail = NULL;
}

/* Adds a process to the back of the queue */
void enq (struct queue *q, struct pcb *p) {
	if(q->head == NULL)
		q->head = p;
	else
		q->tail->next = p;
	q->tail = p;
}

/* Removes a process from the front of the queue */
struct pcb* deq (struct queue *q) {
	struct pcb *p = q->head;
	if(q->head == q->tail)
		q->head = q->tail = NULL;
	else
		q->head = p->next;
	return p;
}

/* Checks if a queue is empty */
int qempty (struct queue *q) {
	return (q->head == NULL);
}