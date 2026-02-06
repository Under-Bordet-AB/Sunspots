/**
 * THIS IS TEST CODE!
 **/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

/**
 * @brief Test worker that actually pulses heartbeats to the supervisor.
 */
int main(int argc, char *argv[]) {
	FILE *fptr = fopen("output.envar", "w");
    if (argc < 3) {
		fprintf(fptr, "DEBUG: To few args?\n");
        return EXIT_FAILURE;
    }

	if (!fptr) return EXIT_FAILURE;

	char *envstr = getenv("TEST");

	if (!envstr) {
		fprintf(fptr, "DEBUG: getenv returned NULL\n");
	} else {
		fprintf(fptr, "DEBUG: getenv found: %s\n", envstr);
	}

	fclose(fptr);
    pid_t ppid = (pid_t)atoi(argv[1]);
    int speed = atoi(argv[2]);   
    int heartbeat_sig = SIGRTMIN; 
	double sum = 10.0;
	int i = 0;
    while (1) {
		i++;
		sum *= sqrt(sum) * pow(sum, sum);
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
