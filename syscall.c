#include "syscall.h"

int KernelFork() {
    return 0;
}

int KernelExec() {
    return 0;
}

void KernelExit() {

}

int KernelWait() {
    return 0;
}

int KernelGetPid() {
    TracePrintf(0, "GetPid: '%d'\n", active.pid);
    return active.pid;
}

int KernelBrk() {
    return 0;
}

int KernelDelay() {
    return 0;
}

int KernelTtyRead() {
    return 0;
}

int KernelTtyWrite() {
    return 0;
}