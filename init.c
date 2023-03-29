#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include <comp421/hardware.h>
#include <comp421/yalnix.h>


int main() {
    TracePrintf(25, "init running ...\n");

    unsigned int pid;
    void *curr_break;
    int i, len;
    char buffer[8];

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

    // Tests the TtyRead() system call
    len = TtyRead(0, buffer, 8);
    if(len == 7 && strncmp(buffer, "yalnix", 6) == 0)
        TracePrintf(25, "init: read 'yalnix' from console\n");

    // Tests the TtyWrite() system call
    len = TtyWrite(0, "yalnix", 6);
    if(len == 6)
        TracePrintf(25, "init: wrote 'yalnix' to console\n");

    // Tests the Exit() system call
    Exit(0);
}