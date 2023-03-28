#include <stdio.h>
<<<<<<< HEAD
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

    Exit(0);

    // return 0;
}
=======
#include <stdlib.h>
#include <comp421/yalnix.h>
#include <comp421/hardware.h>

#define MAX_ARGC	32

int
StartTerminal(int i)
{
    char *cmd_argv[MAX_ARGC];
    char numbuf[128];	/* big enough for %d */
    int pid;

    if (i == TTY_CONSOLE)
	cmd_argv[0] = "console";
    else
	cmd_argv[0] = "shell";
    sprintf(numbuf, "%d", i);
    cmd_argv[1] = numbuf;
    cmd_argv[2] = NULL;

    TracePrintf(0, "Pid %d calling Fork\n", GetPid());
    pid = Fork();
    TracePrintf(0, "Pid %d got %d from Fork\n", GetPid(), pid);

    if (pid < 0) {
	TtyPrintf(TTY_CONSOLE,
	    "Cannot Fork control program for terminal %d.\n", i);
	return (ERROR);
    }

    if (pid == 0) {
	Exec(cmd_argv[0], cmd_argv);
	TtyPrintf(TTY_CONSOLE,
	    "Cannot Exec control program for terminal %d.\n", i);
	Exit(1);
    }

    TtyPrintf(TTY_CONSOLE, "Started pid %d on terminal %d\n", pid, i);
    return (pid);
}

int
main(int argc, char **argv)
{
    int pids[NUM_TERMINALS];
    int i;
    int status;
    int pid;

    for (i = 0; i < NUM_TERMINALS; i++) {
	pids[i] = StartTerminal(i);
	if ((i == TTY_CONSOLE) && (pids[TTY_CONSOLE] < 0)) {
	    TtyPrintf(TTY_CONSOLE, "Cannot start Console monitor!\n");
	    Exit(1);
	}
    }

    while (1) {
	pid = Wait(&status);
	if (pid == pids[TTY_CONSOLE]) {
	    TtyPrintf(TTY_CONSOLE, "Halting Yalnix\n");
	    /*
	     *  Halt should normally be a privileged instruction (and
	     *  thus not usable from user mode), but the hardware
	     *  has been set up to allow it for this project so that
	     *  we can shut down Yalnix simply here.
	     */
	    Halt();
	}
	for (i = 1; i < NUM_TERMINALS; i++) {
	    if (pid == pids[i]) break;
	}
	if (i < NUM_TERMINALS) {
	    TtyPrintf(TTY_CONSOLE, "Pid %d exited on terminal %d.\n", pid, i);
	    pids[i] = StartTerminal(i);
	}
	else {
	    TtyPrintf(TTY_CONSOLE, "Mystery pid %d returned from Wait!\n", pid);
	}
    }
}
>>>>>>> tmp
