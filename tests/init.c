#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#include <comp421/hardware.h>
#include <comp421/yalnix.h>


int main() {
    TracePrintf(25, "init running ...\n");

    // unsigned int pid;
    // void *curr_break;
    // int i;

    // // Tests the GetPid() system call
    // pid = GetPid();
    // TracePrintf(25, "init: pid %d\n", pid);

    // // Tests the Brk() system call
    // curr_break = sbrk(0);
    // curr_break = (void*)((uintptr_t) curr_break + 5);
    // if(!Brk(curr_break))
    //     TracePrintf(25, "init: set program break to 0x%x\n", (uintptr_t) curr_break);
    // curr_break = (void*)((uintptr_t) curr_break - 5);
    // if(!Brk(curr_break))
    //     TracePrintf(25, "init: restored program break to 0x%x\n", (uintptr_t) curr_break);

    // // Tests the Delay() system call
    // for(i = 0; i < 5; i++) {
    //     TracePrintf(25, "init: delay #%d for 5 clock ticks\n", i);
    //     Delay(5);
    // }

    // while(1)
    //     Pause();
    // return 0;
    // TracePrintf(2, "init!\n");

    // TracePrintf(2, "init: main %p\n", main);
    // TracePrintf(2, "pid %d\n", GetPid());
    // int i;

    // for (i = 0; i < 5; i++) {
    //     TracePrintf(2, "Delay # %d for 5 ticks\n", i);
    //     Delay(5);
    // }
    setbuf(stdout, NULL);

    printf("FORK1> This program is a simple test of Fork()\n");
    printf("FORK1> BEFORE Fork(): If nothing else is printed, Fork fails \n");

    if (Fork() == 0) {
	printf("CHILD %d\n", GetPid());
    }
    else {
	printf("PARENT %d\n", GetPid());
	Delay(8);
	printf("FORK1 %d> You should have seen \"CHILD\" and \"PARENT\" printed\n", GetPid());
	printf("FORK1 %d> in the order in which they were scheduled\n", GetPid());
	printf("FORK1 %d> If you missed one or the other, the kernel\n", GetPid());
	printf("FORK1 %d> does NOT switch contexts!!!\n", GetPid());

    }

    while(1) {
        TracePrintf(20, "idle running ...\n");
        Pause();
    }

    // return 0;
}