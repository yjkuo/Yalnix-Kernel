#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main() {
    TracePrintf(2, "init!\n");

    TracePrintf(2, "init: main %p\n", main);
    TracePrintf(2, "pid %d\n", GetPid());

    void *currbreak;
    char *new;

    currbreak = sbrk(0);

    fprintf(stderr, "sbrk(0) = %p\n", currbreak);

    currbreak = (void *)UP_TO_PAGE(currbreak);
    currbreak++;
    currbreak = (void *)UP_TO_PAGE(currbreak);

    if (Brk(currbreak)) {
	fprintf(stderr, "Brk %p returned error\n", currbreak);
	Exit(1);
    }

    currbreak++;
    currbreak = (void *)UP_TO_PAGE(currbreak);

    if (Brk(currbreak)) {
	fprintf(stderr, "Brk %p returned error\n", currbreak);
	Exit(1);
    }

    new = malloc(10000);

    Exit(0);
    printf("hello world\n");
    // printf("this %d is %d a %d test %d\n", a, b, c, d);
    return 0;
}