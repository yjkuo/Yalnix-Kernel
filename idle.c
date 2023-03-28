#include <comp421/hardware.h>

int main() {
    while(1) {
        TracePrintf(20, "idle running ...\n");
        Pause();
    }
    return 0;
}