#include <stdio.h>
// #include <stdlib.h>
/* #include "which.h" */
/* #ifdef ON_LOCAL_PC */
/* #include "yalnix.h" */
/* #else */
#include <comp421/yalnix.h>
/* #endif */

void
recurse(char *who, int i)
{
    char waste[1024];	/* use up stack space in the recursion */
    char *mem = &waste[100];//(char *)malloc(2048); /* use up heap space */
    int j = 0;
    
    printf("mem = %p\n", mem);

    for (j = 0; j < 1024; j++)
    	waste[j] = 'a';

    (void) waste[1023];
    // Delay(1);

    printf("%s %d\n", who, i);
    if (i == 0)
    {
	printf("Done with recursion\n");
	return;
    }
    else
	recurse(who, i - 1);
}

int
main()
{
    int pid;

    setbuf(stdout, NULL);

    printf("BEFORE\n");

    if ((pid = 0) == 0)
    {
	printf("CHILD\n");
	recurse("child", 33);
    }
    else
    {
	printf("PARENT: child pid = %d\n", pid);
	recurse("parent", 33);
    }
}