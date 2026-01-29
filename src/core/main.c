/**
 * main.c - pid supervisor, flat model
 **/

/*
  TO DO
  Make daemon go dark, done
  what logger to use? syslog, done
  add health data to when process is reaped, done (maybe more info?)
  
 */

#define _GNU_SOURCE
#include "daemon.h"
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/syslog.h>

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

	/* Get working directory for exec path.
	   This needs to be solved with another soluton
	   when each binary is in its separate directory
	 */
	char prj_path[1024];
	getcwd(prj_path, sizeof(prj_path));
	
	/**
	 * THE DAEMON DETACHMENT RITUAL
	 * Step 1: First fork to get our child
	 * to run in the background,
	 **/
    pid_t init_pid = fork();
    if (init_pid < 0)
    {
        perror(" !! 1st fork failed");
        exit(EXIT_FAILURE);
    }
    if (init_pid > 0)
    {
        printf(" >> Daemon is now running in the background.\n");
        exit(EXIT_SUCCESS);
    }
	/**
	 * Step 2: We are now the first child.
	 * Become session leader, Become session leader.
	 * This ensures that when the user closes 
     * their terminal window, we don't get taken down with it.
	 **/
	else
	{
		if (setsid() < 0)
		{
			perror(" !! setsid failed");
			exit(EXIT_FAILURE);
		}
		pid_t second_pid = fork();
		if (second_pid < 0)
		{
			perror(" !! 2th fork failed");
			exit(EXIT_FAILURE);
		}
		else if (second_pid > 0)
		{
			/**
			 * Step 3: The job of the first spawned child
			 * is done. Our child is not a session leader
			 * and can not be re-acquired by a terminal.
			 * Time to rest.
			 **/
			 exit(EXIT_SUCCESS);
		}
		/**
		 * Step 4: Send the daemon into the void
		 * blind and deaf.
		 **/
		else
		{
			printf(" >> Daemon cannot be re-aqcuired by terminal.\n"
				   " >> Daemon is now entering the dark and empty void.\n"
				   " !! USE 'kill -SIGINT %i' TO KILL IT!\n", getpid());
			umask(0);                       /* Our daemon has no rights */
			if (chdir("/") != 0)            /* Change wd to root */
			{
				perror(" !! chdir failed");
				exit(EXIT_FAILURE);
			}
			close(STDIN_FILENO);
			close(STDOUT_FILENO);
			close(STDERR_FILENO);
			int scream_into_the_void = open("/dev/null", O_RDWR);
			dup2(scream_into_the_void, STDIN_FILENO);
			dup2(scream_into_the_void, STDOUT_FILENO);
			dup2(scream_into_the_void, STDERR_FILENO);
		}
	}
	/**
	 * From here we only run dark and foreboding daemon code.
	 * Very, fucking, metal.
	 **/

	openlog("SUNSPOTS_DAEMON", LOG_PID, LOG_DAEMON);
	syslog(LOG_NOTICE, "Sunspots daemon started successfully. Detached and darkened.");
    daemon_signal_setup();

	/* This like CWD needs a better solution */
	char core_path[1024];
	char fetch_path[1024];
	// char server_path[1024];
	snprintf(core_path, sizeof(core_path), "%s/sunspots_core", prj_path);
	snprintf(fetch_path, sizeof(fetch_path), "%s/fetch_data", prj_path);
    spawn_process(0, core_path, "Sunspots_core", 2);
    spawn_process(1, fetch_path, "Fetch_data", 1);
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
        for (int i = 0; i < active_processes; i++)
        {
            int status;
            pid_t rv = waitpid(watch_table[i].pid, &status, WNOHANG);
            int needs_restart = 0;
            if (rv > 0)
            {
                syslog(LOG_ERR, "Process: %s PID: %i terinated. Restarting.", 
                       watch_table[i].name, watch_table[i].pid);
                needs_restart = 1;
            }
            else if (!watch_table[i].alive)
            {
               /* The process is still "running" according to the OS, but it 
                  hasn't sent a heartbeat signal. It's likely deadlocked or hung. */
				syslog(LOG_ERR, "Process: %s PID: %i hung. Restarting.",
                       watch_table[i].name, watch_table[i].pid);
                kill(watch_table[i].pid, SIGKILL);
                waitpid(watch_table[i].pid, NULL, 0);				
				// Can add getrusage() here to print how much resources was used to log
				struct rusage usage;
				if (getrusage(RUSAGE_CHILDREN, &usage) == 0)
				{
					// On Linux, ru_maxrss is in Kilobytes.
					syslog(LOG_INFO, "Total Child RAM Peak: %ld KB", usage.ru_maxrss);
					// CPU time is split into seconds and microseconds
					syslog(LOG_INFO, "Total User CPU Time: %ld.%06ld sec",
						   usage.ru_utime.tv_sec, usage.ru_utime.tv_usec);
				}
                needs_restart = 1;
            }
            else
            {
                syslog(LOG_INFO, "Process: %s PID: %i is healthy.",
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
            syslog(LOG_INFO, "Process: %s PID: %i reaped\n", watch_table[i].name, watch_table[i].pid);
			struct rusage usage;
			if (getrusage(RUSAGE_CHILDREN, &usage) == 0)
			{
				// On Linux, ru_maxrss is in Kilobytes.
				syslog(LOG_INFO, "Total Child RAM Peak: %ld KB", usage.ru_maxrss);
				// CPU time is split into seconds and microseconds
				syslog(LOG_INFO, "Total User CPU Time: %ld.%06ld sec",
					   usage.ru_utime.tv_sec, usage.ru_utime.tv_usec);
			}
        }
    }    

	syslog(LOG_NOTICE, "Daemon vanished. All children reaped");
	closelog();
    return EXIT_SUCCESS;        
}

void spawn_process(int index, const char *path, const char *name, int speed)
{
    watch_table[index].path = path;
    watch_table[index].name = name;
    watch_table[index].heartbeat_speed = speed > 0 ? speed : HEARTBEAT_SPEED;
    watch_table[index].alive = 1;

	int pipefd[2];
	if (pipe2(pipefd, O_CLOEXEC) == -1)
	{
		perror("creating error pipe failed");
		return;
	}	
	
    pid_t p = fork();
    if (p < 0)
    {
		close(pipefd[0]);
		close(pipefd[1]);
        perror("Spawning fork failed");
        return;
    }

    if (p == 0)
    {		
        /* CHILD CONTEXT */
		close(pipefd[0]); /* Children don't read */
		/* ADD SETENV WITH JSON BLOB HERE */
        char p_pid_str[32];
        char p_hspeed_str[32];
        
        /* Pass the Parent PID so the worker knows who to signal */
        sprintf(p_pid_str, "%d", getppid());
        sprintf(p_hspeed_str, "%d", speed);
        char *args[] = { (char*)path, p_pid_str, p_hspeed_str, NULL };        
        execvp(args[0], args);
        /* If execvp returns, something went horribly wrong */
        perror(" !! Execvp failed! Sending errno through pipe and stopping daemon\n"
			   " !! ");
		int err = errno;
		write(pipefd[1], &err, sizeof(err));
		close(pipefd[1]);
        exit(EXIT_FAILURE);
    }
    else
    {
		/* DAEMON CONTEXT */
		close(pipefd[1]); /* Parents never write */
		int err = 0;
		if (read(pipefd[0], &err, sizeof(err)) > 0)
		{
			g_daemon_running = 0;
		}
        watch_table[index].pid = p;
    }
}

void daemon_shutdown_handler(int sig)
{
	// Change to write(STDOUT,, ...) 
    syslog(LOG_INFO,"Daemon killed.");
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
