/**
 * Sunspots - main.c
 **/

/*
  1. Start program with arg daemon
  2. Program starts and forks then terminates parent process
  3. Parentless process is picked up by systemd and becomes "Health daemon"
  4. "Health daemon" forks and starts the actual program (Sunspots)
  5. Parent monitors child and can restart if process hangs
 */

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

/*
  Move these elsewhere at some point
  Child must prove it is still alive
  every 2 seconds
  After 5 seconds daemon kills > restarts
 */

#define CHECKIN_TIMEOUT 5
#define CHECKIN_INTERVAL 2

static int g_daemon_running = 1;
static pid_t g_child_pid = -1;
static volatile sig_atomic_t g_checkin_received = 0;

void daemon_handler(int sig);
void checkin_handler(int sig);
void checkin_timeout_handler(int sig);
void run_sunspots();

int main(int argc, char *argv[])
{
	if (argc < 2 || strcmp(argv[1], "daemon") != 0)
	{
		printf("Usage: %s daemon\n"
			   "Starts program with watchful daemon\n", argv[0]);
		return EXIT_FAILURE;
	}

	struct sigaction sa_checkin = {0};
	sa_checkin.sa_handler = checkin_handler;
	sigemptyset(&sa_checkin.sa_mask);
	sa_checkin.sa_flags = SA_RESTART;
	sigaction(SIGUSR1, &sa_checkin, NULL);

	struct sigaction sa_alarm = {0};
	sa_alarm.sa_handler = checkin_timeout_handler;
	sigemptyset(&sa_alarm.sa_mask);
	sa_alarm.sa_flags = SA_RESTART;
	sigaction(SIGALRM, &sa_alarm, NULL);
	
	signal(SIGTERM, daemon_handler);
	signal(SIGINT, daemon_handler);
	while (g_daemon_running)
	{
		g_child_pid = fork();
		if (g_child_pid < 0) exit(EXIT_FAILURE);
		if (g_child_pid == 0)
		{			
			run_sunspots();
			exit(0);
		}
		else
		{
			alarm(CHECKIN_TIMEOUT);
			/* Daemon time */
			while (1)
			{
				g_checkin_received = 0;
				usleep(1000);
				int status;
				/* return */
				pid_t result = waitpid(g_child_pid, &status, WNOHANG);
				if (result > 0)
				{
					printf("Sunspots stopped. Restarting.\n");
					break;
				}
				if (g_checkin_received)
				{
					printf("Sunspot checked in with watchful daemon.\n");
					g_checkin_received = 0;
					/* reset alarm*/
					alarm(CHECKIN_TIMEOUT);
					
				}
				else
				{
					/* Sunspots is probably killed */
				}
				
			}
		}
	}
	
	return EXIT_SUCCESS;
}

void daemon_handler(int sig)
{
	g_daemon_running = 0;
	printf("Daemon slayed.\n");
}

void checkin_handler(int sig)
{
	g_checkin_received = 1;
}

void checkin_timeout_handler(int sig)
{
	if (!g_checkin_received)
	{
		printf("Watchful daemon killing unresponsive child with PID: %i\n", g_child_pid);
		kill(g_child_pid, SIGKILL);
	}
}

/* Here's where the actual program process runs */
void run_sunspots()
{
	printf("Sucessfully started Sunspots process with PID: %i\n", getpid());
	/* for testing */
	int lc = 0;
	while (1)
	{
		lc++;
		/* signal parent that we're A-OK */
		kill(getppid(), SIGUSR1);
		printf("Child called home.\n");
		if (lc == 4)
		{
			printf("Simulating hang.\n");
			while(1);
		}
		sleep(CHECKIN_INTERVAL);
	}
}
