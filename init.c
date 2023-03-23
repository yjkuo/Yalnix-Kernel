#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#include <comp421/hardware.h>
#include <comp421/yalnix.h>


int main() {
    TracePrintf(25, "init running ...\n");

    unsigned int pid;
    void *curr_break;
    int i;

    // Tests the GetPid() system call
    pid = GetPid();
    TracePrintf(25, "init: pid %d\n", pid);

    // Tests the Brk() system call
    curr_break = sbrk(0);
    curr_break = (void*)((uintptr_t) curr_break + 5);
    if(!Brk(curr_break))
        TracePrintf(25, "init: set program break to 0x%x\n", (uintptr_t) curr_break);
    curr_break = (void*)((uintptr_t) curr_break - 5);
    if(!Brk(curr_break))
        TracePrintf(25, "init: restored program break to 0x%x\n", (uintptr_t) curr_break);

    // Tests the Delay() system call
    for(i = 0; i < 5; i++) {
        TracePrintf(25, "init: delay #%d for 5 clock ticks\n", i);
        Delay(5);
    }

    while(1)
        Pause();
    return 0;
}