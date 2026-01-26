/**
 * daemon.c - pid supervisor, flat model
 **/

#include "daemon.h"
#include <time.h>

watch_entry_t watch_table[MAX_CHILDREN] = {0};
int active_processes = 0;
volatile sig_atomic_t g_daemon_running = 1;

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "daemon") != 0)
    {
        printf(" !! Usage: %s daemon\n", argv[0]);
        return EXIT_FAILURE;
    }
    /* Initial fork: This cuts the cord from the terminal. 
       The parent exits, making the shell think the job is done. */
    pid_t init_pid = fork();
    if (init_pid < 0)
    {
        perror(" !! Initial fork failed");
        exit(EXIT_FAILURE);
    }
    if (init_pid > 0)
    {
        printf(" >> Sunspots daemon successfully detached and running with PID: %i\n", init_pid);
        exit(EXIT_SUCCESS);
    }

    /* Become session leader: This ensures that when the user closes 
       their terminal window, we don't get taken down with it. */
    if (setsid() < 0)
    {
        perror(" !! Daemon failed to become session leader");
        exit(EXIT_FAILURE);
    }

    daemon_signal_setup();

    /* Bootstrap the worker processes.
       Can get these from config leater
	   And we'll loop I guess. Or stick to this? */
    spawn_process(0, "./sunspots_core", "Sunspots_core", 2);
    spawn_process(1, "./fetch_data", "Fetch_data", 1);
    active_processes = 2;

    /* THE MONITORING LOOP */
    while (g_daemon_running)		
    {
		/* Needed to sleep correctly even when reciving
		   signals from childen */
		struct timespec req = {HEALTH_CHECKUP_INTERVAL, 0}; /* Requested sleep time: s, ns */
		struct timespec rem; /* kernel writes to this var how much time is left on requested sleep when interreputed */
		while (nanosleep(&req, &rem) == -1)
		{	   
			if (!g_daemon_running) break;
			req = rem; /* If we wake up, don't do anything until time is up */
		}
		if (!g_daemon_running) break;
        printf("\n >> Daemon checking health of processes...\n"
			   " !! Use 'kill -SIGINT %i to slay daemon.\n\n", getpid());
        for (int i = 0; i < active_processes; i++)
        {
            int status;
            pid_t rv = waitpid(watch_table[i].pid, &status, WNOHANG);
            int needs_restart = 0;
            if (rv > 0)
            {
                printf(" !! %-15s | PID: %-8i | Status: TERMINATED! Restarting.\n",
                       watch_table[i].name, watch_table[i].pid);
                needs_restart = 1;
            }
            else if (!watch_table[i].alive)
            {
               /* The process is still "running" according to the OS, but it 
                  hasn't sent a heartbeat signal. It's likely deadlocked or hung. */
                printf(" !! %-15s | PID: %-8i | Status: HUNG! Restarting.\n",
                       watch_table[i].name, watch_table[i].pid);
                
                kill(watch_table[i].pid, SIGKILL);
                waitpid(watch_table[i].pid, NULL, 0);
                needs_restart = 1;
            }
            else
            {
                printf(" >> %-15s | PID: %-8i | Status: A-OKAY!.\n",
                       watch_table[i].name, watch_table[i].pid);
                watch_table[i].alive = 0; 
            }

            if (needs_restart && g_daemon_running)
            {
                spawn_process(i, watch_table[i].path, watch_table[i].name, watch_table[i].heartbeat_speed);                
            }
        }
    }

    for (int i = 0; i < active_processes; i++)
    {
        pid_t target = watch_table[i].pid;
        if (kill(target, SIGTERM) == 0)
        {
            waitpid(target, NULL, 0);
            printf(" >> Reaped process: %i\n", target);
        }
    }    

    return EXIT_SUCCESS;        
}

void spawn_process(int index, const char *path, const char *name, int speed)
{
    watch_table[index].path = path;
    watch_table[index].name = name;
    watch_table[index].heartbeat_speed = speed > 0 ? speed : HEARTBEAT_SPEED;
    watch_table[index].alive = 1;

    pid_t p = fork();
    if (p < 0)
    {
        perror("Spawning fork failed");
        return;
    }

    if (p == 0)
    {
        /* CHILD CONTEXT */
        char p_pid_str[32];
        char p_hspeed_str[32];
        
        /* Pass the Parent PID so the worker knows who to signal */
        sprintf(p_pid_str, "%d", getppid());
        sprintf(p_hspeed_str, "%d", speed);
        char *args[] = { (char*)path, p_pid_str, p_hspeed_str, NULL };        
        execvp(args[0], args);
        /* If execvp returns, something went horribly wrong */
        perror(" !! Execvp failed!");
        exit(EXIT_FAILURE);
    }
    else
    {
        watch_table[index].pid = p;
    }
}

void daemon_shutdown_handler(int sig)
{
	// Change to write(STDOUT,, ...) 
    printf("\n >> Daemon slayed, you gained +666 XP!\n");
    g_daemon_running = 0;
}

void heartbeat_handler(int sig, siginfo_t *info, void *context)
{
    pid_t sender = info->si_pid;
    for (int i = 0; i < active_processes; i++)
    {
        if (watch_table[i].pid == sender)
        {
            watch_table[i].alive = 1;
            return;
        }
    }
}

void daemon_signal_setup()
{
    struct sigaction sa_rt = {0};
    sa_rt.sa_sigaction = heartbeat_handler;
    sigemptyset(&sa_rt.sa_mask);
    /* SA_RESTART: Prevents heartbeats from breaking the sleep() in our loop */
    sa_rt.sa_flags = SA_SIGINFO | SA_RESTART;
    
    if (sigaction(HEARTBEAT_SIG, &sa_rt, NULL) != 0)
    {
        perror("Failed to setup signal handler for process");
        exit(EXIT_FAILURE);
    }

    struct sigaction sa_term = {0};
    sa_term.sa_handler = daemon_shutdown_handler;
    sigemptyset(&sa_term.sa_mask);
    /* No SA_RESTART here: We want SIGINT/SIGTERM to wake the loop immediately */
    sa_term.sa_flags = 0;
    
    sigaction(SIGTERM, &sa_term, NULL);
    sigaction(SIGINT, &sa_term, NULL);
}
