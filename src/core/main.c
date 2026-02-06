/**
 * main.c - pid supervisor, flat model
 **/

#define _GNU_SOURCE

#include "daemon.h"
#include "../../src/libs/json/cJSON.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

#define CONFIG_WD "../config/config.json"

watch_entry_t *watch_table = NULL;
int active_processes = 0;
volatile sig_atomic_t g_daemon_running = 1;

char *read_conf(char *filepath);
void  set_env(char **config);

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "daemon") != 0)
    {
        printf(" !! Usage: %s daemon\n", argv[0]);
        return EXIT_FAILURE;
    }

	char prj_path[1024];
	getcwd(prj_path, sizeof(prj_path));
	
	DAEMONIZE();

	openlog("SUNSPOTS_DAEMON", LOG_PID, LOG_DAEMON);
	syslog(LOG_NOTICE, "Sunspots daemon started successfully. Detached and darkened.");
    daemon_signal_setup();

	/* Read config */
	char abs_conf_path[1024];
	snprintf(abs_conf_path, sizeof(abs_conf_path), "%s/%s", prj_path, CONFIG_WD);
	char *config_data = read_conf(abs_conf_path);
	if (!config_data)
	{
		syslog(LOG_ERR, "Could not read config file.");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Read config file from path.");
	cJSON *root = cJSON_Parse(config_data);
	if (!root)
	{
		const char *err_ptr = cJSON_GetErrorPtr();
		if (err_ptr != NULL)
		{
			syslog(LOG_ERR, "JSON Syntax error before: %s", err_ptr);
		}
		exit(EXIT_FAILURE);		
	}
	cJSON *modules = cJSON_GetObjectItemCaseSensitive(root, "modules");
	if (!cJSON_IsArray(modules))
	{
		syslog(LOG_ERR, "JSON Parse Error before: %s", cJSON_GetErrorPtr());
		cJSON_Delete(root);
		free(config_data);
		exit(EXIT_FAILURE);
	}
		
	active_processes = cJSON_GetArraySize(modules);		
	watch_table = calloc(active_processes, sizeof(watch_entry_t));
	if (!watch_table)
	{
		syslog(LOG_CRIT, "Calloc failed.");
		exit(EXIT_FAILURE);
	}

	cJSON *module = NULL;
	int i = 0;   
	cJSON_ArrayForEach(module, modules)
	{
		cJSON *name = cJSON_GetObjectItemCaseSensitive(module, "name");
		cJSON *path = cJSON_GetObjectItemCaseSensitive(module, "bin_path");
		cJSON *hb   = cJSON_GetObjectItemCaseSensitive(module, "heartbeat_interval");
		if (cJSON_IsString(name) && cJSON_IsString(path) && cJSON_IsNumber(hb))
		{
			watch_table[i].name = strdup(name->valuestring);
			watch_table[i].path = strdup(path->valuestring);
			watch_table[i].heartbeat_speed = hb->valueint;
			watch_table[i].config = cJSON_PrintUnformatted(module);
			spawn_process(i, watch_table[i].path, watch_table[i].name, watch_table[i].heartbeat_speed, prj_path);
			i++;
		}
	}
	cJSON_Delete(root);
	free(config_data);
    /* THE MONITORING LOOP */
    while (g_daemon_running)		
    {
		/* Needed to sleep correctly even when reciving signals from childen */
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
                syslog(LOG_ERR, "Process: %s PID: %i terinated. Restarting.", watch_table[i].name, watch_table[i].pid);
                needs_restart = 1;
            }
            else if (!watch_table[i].alive)
            {
               /* The process is still "running" according to the OS, but it hasn't sent a heartbeat signal. It's likely deadlocked or hung. */
				syslog(LOG_ERR, "Process: %s PID: %i hung. Restarting.", watch_table[i].name, watch_table[i].pid);				
                kill(watch_table[i].pid, SIGKILL);
                waitpid(watch_table[i].pid, NULL, 0);				
				struct rusage usage;
				if (getrusage(RUSAGE_CHILDREN, &usage) == 0)
				{
					syslog(LOG_INFO, "Total Child RAM Peak: %ld KB", usage.ru_maxrss);
					syslog(LOG_INFO, "Total User CPU Time: %ld.%06ld sec", usage.ru_utime.tv_sec, usage.ru_utime.tv_usec);
				}
                needs_restart = 1;
            }
            else
            {
                syslog(LOG_INFO, "Process: %s PID: %i is healthy.", watch_table[i].name, watch_table[i].pid);
                watch_table[i].alive = 0; 
            }

            if (needs_restart && g_daemon_running)
            {
                spawn_process(i, watch_table[i].path, watch_table[i].name, watch_table[i].heartbeat_speed, prj_path);                
            }
        }
    }

    for (int i = 0; i < active_processes; i++)
    {
        //pid_t target = watch_table[i].pid;
		if (watch_table[i].pid > 0)
		{
			if (kill(watch_table[i].pid, SIGTERM) == 0)
			{
				waitpid(watch_table[i].pid, NULL, 0);
			}
			else
			{
				waitpid(watch_table[i].pid, NULL, WNOHANG);
			}
		}
		if (watch_table[i].name)   { free((void*)watch_table[i].name);   watch_table[i].name = NULL; }
        if (watch_table[i].path)   { free((void*)watch_table[i].path);   watch_table[i].path = NULL; }
        if (watch_table[i].config) { free((void*)watch_table[i].config); watch_table[i].config = NULL; }
    }
    free(watch_table);
    watch_table = NULL;
    active_processes = 0;
	
	syslog(LOG_NOTICE, "Daemon vanished. All children reaped");
	closelog();
	
    return EXIT_SUCCESS;        
}

void spawn_process(int index, const char *path, const char *name, int speed, const char *wd)
{
	chdir(wd); /* Move back to prj dir from root for relative path to processes */
    watch_table[index].path = path;
    watch_table[index].name = name;
    watch_table[index].heartbeat_speed = speed > 0 ? speed : HEARTBEAT_SPEED;
    watch_table[index].alive = 1;

	int pipefd[2];
	if (pipe2(pipefd, O_CLOEXEC) == -1)
	{
		syslog(LOG_ERR, "spawn_process: pipe failed");
		return;
	}
	
    pid_t p = fork();
    if (p < 0)
    {
		close(pipefd[0]);
		close(pipefd[1]);
		syslog(LOG_ERR, "spawn_process: fork failed");
        return;
    }

    if (p == 0)
    {		
        /* CHILD CONTEXT */
		close(pipefd[0]);
		chdir(wd);
		if (watch_table[index].config != NULL)
		{
			/* Contains all fields for that module */
			setenv("SUNSPOTS_CONFIG", watch_table[index].config, 1);
		}
		char p_pid_str[32];
        char p_hspeed_str[32];
		sprintf(p_pid_str, "%d", getppid());
		sprintf(p_hspeed_str, "%d", speed);
        char *args[] = { (char*)path, p_pid_str, p_hspeed_str, NULL };        
        execvp(args[0], args);
		syslog(LOG_ERR, "Execvp failed!");
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
		close(pipefd[0]);
        watch_table[index].pid = p;
    }
}

/* 1. info->si_pid provides kernel-verified identity of the signaling worker. */
/* 2. Handler must be 'Async-Signal-Safe'; avoid printf/malloc here in production. */
/* 3. Flips the 'alive' latch; the main loop will clear this bit every checkup. */
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

/* 1. Uses SA_SIGINFO to enable metadata (PID) collection for heartbeats. */
/* 2. SA_RESTART ensures heartbeats don't break the daemon's nanosleep timing. */
/* 3. SIGINT/SIGTERM intentionally omit SA_RESTART to force immediate loop exit. */
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

void daemon_shutdown_handler(int sig)
{
    syslog(LOG_INFO,"Daemon killed.");
    g_daemon_running = 0;
}

char *read_conf(char *filepath)
{
	FILE *fptr = fopen(filepath, "rb");
	if (!fptr) return NULL;
	
	
	fseek(fptr, 0, SEEK_END); /* Sets file pos indicator to end of file */
	size_t len = ftell(fptr); /* Gets value of file pos indicator */
	rewind(fptr); /* rewinds pos indicator */

	char *buf = (char*)malloc(len + 1);
	if (!buf)
	{
		fclose(fptr);
		return NULL;
	}
	
	size_t rb = fread(buf, 1, len, fptr);
	if (rb < len)
	{
		if (ferror(fptr))
		{
			syslog(LOG_ERR, "Error reading file.");
			free(buf);
			return NULL;
		}
		else if (feof(fptr))
		{
			buf[rb] = '\0';
		}
	}
	else
	{
		buf[len] = '\0';
	}

	fclose(fptr);
	return buf;
}

void set_env(char **config)
{
	if (setenv("SUNSPOTS_CONFIG", *config, 1) != 0)
	{
		syslog(LOG_ERR, "Could not load config into environment.");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Loaded config into environment.");
}
