#include <comp421/hardware.h>

int main() {
    TracePrintf(1, "Idle start running...\n");
    while (1) {
        Pause();
    }
    return 0;
}