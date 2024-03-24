#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#define NUM_LOOKUPS 26

void signal_handler(int signum) {
    ;
}

int main(int argc, char *argv[]) {
    int nums[NUM_LOOKUPS];
    int ppid;
    
    printf("argv[1] = %s\n", argv[1]);
    sscanf(argv[2], "--parent-pid=%d", &ppid);
    printf("ppid = %d\n", ppid);

    signal(SIGUSR1, signal_handler); // Set signal handler for SIGUSR1
    for (int i = 0; i < NUM_LOOKUPS; i++) {
        kill(ppid, SIGUSR1);
        pause();

        int n = nums[i];

        kill(ppid, SIGUSR1);
        pause();
        printf("nums[%d] = %d\n", i, n);
    }
    return 0;
}