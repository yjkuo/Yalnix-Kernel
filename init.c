#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdio.h>

int main() {
    TracePrintf(2, "init!\n");

    TracePrintf(2, "init: main %p\n", main);
    TracePrintf(2, "pid %d\n", GetPid());

    printf("hello world\n");
    // printf("this %d is %d a %d test %d\n", a, b, c, d);
    return 0;
}