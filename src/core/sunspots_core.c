/**
 * THIS IS TEST CODE!
 **/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

/**
 * @brief Test worker that actually pulses heartbeats to the supervisor.
 */
int main(int argc, char *argv[]) {
    if (argc < 3) {
        return EXIT_FAILURE;
    }

    pid_t ppid = (pid_t)atoi(argv[1]);
    int speed = atoi(argv[2]);
    
    // In Linux, SIGRTMIN is the start of Real-Time signals
    int heartbeat_sig = SIGRTMIN; 

    printf(" >> [Worker %d] Online. Pulsing every %ds to Parent PID: %d\n", getpid(), speed, ppid);
	double sum = 10.0;
	int i = 0;
    while (1) {
		i++;
		sum *= sqrt(sum);
		if (i == 4) sleep(5);
        /* Send a Real-Time signal to the Parent.
           Because we setup the Daemon with SA_SIGINFO, 
           it will see our PID in the siginfo_t struct. */
        if (kill(ppid, heartbeat_sig) == -1) {
            perror(" !! Worker signal failed");
            exit(EXIT_FAILURE);
        }
        
        sleep(speed);
    }

    return EXIT_SUCCESS;
}
