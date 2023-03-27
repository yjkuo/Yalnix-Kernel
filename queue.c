#include <stdlib.h>

#include "queue.h"


/* Initializes a process queue */
void qinit (struct queue *q) {

	// Marks the queue as empty
	q->head = NULL;
	q->tail = NULL;
	q->size = 0;
}


/* Adds a process to the back of the queue */
void enq (struct queue *q, struct pcb *p) {

	struct pcb_frame *pframe;

	// Creates a new process frame
	pframe = (struct pcb_frame*) malloc(sizeof(struct pcb_frame));
	pframe->proc = p;
	pframe->next = NULL;

	// Updates the head of the queue if necessary
	if(q->size == 0)
		q->head = pframe;

	// Adds the process frame at the tail of the queue
	if(q->size > 0)
		q->tail->next = pframe;
	q->tail = pframe;

	// Increments the size of the queue
	q->size++;
}


/* Removes a process from the front of the queue */
struct pcb* deq (struct queue *q) {

	struct pcb_frame *pframe;
	struct pcb *process;

	// Gets the process at the head of the queue
	pframe = q->head;
	process = pframe->proc;

	// Updates the head of the queue (and the tail if necessary)
	if(q->size == 1)
		q->tail = NULL;
	q->head = q->head->next;

	// Frees the process frame
	free(pframe);

	// Decrements the size of the queue
	q->size--;

	// Returns the process
	return process;
}


/* Peeks at the process at the front of the queue */
struct pcb* peekq (struct queue q) {
	return q.tail->proc;
}


/* Checks if a queue is empty */
int qempty (struct queue q) {
	return (q.size == 0);
}


/* Destroys a process queue */
void qdestroy (struct queue *q) {

	struct pcb_frame *pframe, *delete_pframe;

	// Iterates over each process in the queue
	pframe = q->head;
	while(pframe) {

		// Gets the frame to be deleted
		delete_pframe = pframe;
		pframe = pframe->next;

		// Frees the process frame and the PCB it holds
		free(delete_pframe->proc);
		free(delete_pframe);
	}
}