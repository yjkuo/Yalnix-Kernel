#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main() {
    TracePrintf(2, "init!\n");

    TracePrintf(2, "init: main %p\n", main);
    TracePrintf(2, "pid %d\n", GetPid());
    int i;

    for (i = 0; i < 5; i++) {
        TracePrintf(2, "Delay # %d for 5 ticks\n", i);
        Delay(5);
    }

    return 0;
}