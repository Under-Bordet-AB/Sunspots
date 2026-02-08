/**
 * SUNSPOTS daemon
 * Uses a flat model.
 * Hot re-load if user updates config.
 * TO-DO: PID lock?
 **/

#define _GNU_SOURCE
#include "daemon.h"
#define CONFIG_WD "../../config/sunspots.json"
#define CONFIG_FILENAME "sunspots.json"

watch_entry_t *watch_table = NULL;
int g_active_proc = 0;
volatile sig_atomic_t g_daemon_running = 1;

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "daemon") != 0)
    {
        printf(" !! Usage: %s daemon\n", argv[0]);
        return EXIT_FAILURE;
    }
	char prj_path[1024];
	getcwd(prj_path, sizeof(prj_path));
	char actual_path[PATH_MAX];
	if (realpath(CONFIG_WD, actual_path) == NULL)
	{
		fprintf(stderr, " !! Cannot find config file at: %s: %s\n", CONFIG_WD, strerror(errno));
		return EXIT_FAILURE;
	}
	DAEMONIZE();
	openlog("SUNSPOTS_DAEMON", LOG_PID, LOG_DAEMON);
	syslog(LOG_NOTICE, "Sunspots daemon started successfully. Detached and darkened.");	
    daemon_signal_setup();
	
	char config_dir[1024]; /* Inotify will watch directory */
	strncpy(config_dir, actual_path, sizeof(config_dir));
	char *adapt_to_directory = strrchr(config_dir, '/');
	if (adapt_to_directory) *adapt_to_directory = '\0';

	daemon_reload_config(actual_path, prj_path);

	int epoll_fd = epoll_create1(EPOLL_CLOEXEC); /* Our children don't need fd */
	if (epoll_fd == -1)
	{
		syslog(LOG_ERR, "epoll_create1 failed: %m"); /* %m is replaced by errno msg (strerror(errno)) */
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Sucessfully setup epoll");

	int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (timer_fd == -1)
	{
		syslog(LOG_ERR, "timerfd_create failed: %m");
		exit(EXIT_FAILURE);
	}
	struct itimerspec ts =
	{
		.it_interval = { HEALTH_CHECKUP_INTERVAL, 0 },
		.it_value    = { HEALTH_CHECKUP_INTERVAL, 0 }
	};
	if (timerfd_settime(timer_fd, 0, &ts, NULL) == -1)
	{
		syslog(LOG_ERR, "timerfd_settime failed: %m");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Successfully setup timerfd");

	int i_flags = IN_CLOSE_WRITE | IN_MOVED_TO;
	int inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (inotify_fd == -1)
	{
		syslog(LOG_ERR, "inotify_init1 failed: %m");
		exit(EXIT_FAILURE);
	}
	int dir_to_watch = inotify_add_watch(inotify_fd, config_dir, i_flags);
	if (dir_to_watch == -1)
	{
		syslog(LOG_ERR, "inotify_add_watch failed: %m");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Sucessfully setup inotify to watch directory: %s", config_dir);

	struct epoll_event ev, events[MAX_EVENTS];
	ev.events  = EPOLLIN;
	ev.data.fd = timer_fd;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD , timer_fd, &ev) == -1)
	{
		syslog(LOG_ERR, "epoll_ctl failed: %m");
		exit(EXIT_FAILURE);
	}
	ev.events  = EPOLLIN;
	ev.data.fd = inotify_fd;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, inotify_fd, &ev) == -1)
	{
		syslog(LOG_ERR, "epoll_ctl failed: %m");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "SUNSPOTS daemon setup complete! Waiting for event...");

	while (g_daemon_running)
	{
		int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
		if (nfds == -1)
		{
			if (errno == EINTR) continue;
			syslog(LOG_ERR, "epoll_wait error: %m");
			break;
		}
		for (int i = 0; i < nfds; i++)
		{
			/* timerfd triggered: health checkup*/
			if (events[i].data.fd == timer_fd)
			{
				uint64_t exp;
				if (read(timer_fd, &exp, sizeof(exp)) > 0)
				{
					daemon_perform_health_check(prj_path);
				}
			}
			/* inotify triggered: hot reload */
			else if (events[i].data.fd == inotify_fd)
			{
				/* GCC compiler directive, also called 'attribute'
				 * Tells the compiler how to lay out this var in mem.
				 * Syntax: type variable __attribute__((directive)); */
				char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
				ssize_t len;
				static time_t last_reload = 0;
				while ((len = read(inotify_fd, buf, sizeof(buf))) > 0)
				{
					/* loop over all the events */
					const struct inotify_event *event;
					for (char *ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) +event->len)
					{
						event = (const struct inotify_event*)ptr;
						if (event->len && strcmp(event->name, CONFIG_FILENAME) == 0)
						{
							time_t now = time(NULL);
							if (now - last_reload >= 1)
							{
								syslog(LOG_NOTICE, "Config change, performing hot-relead.");
								daemon_reload_config(actual_path, prj_path);
								last_reload = now;
							}
							else
							{
								syslog(LOG_INFO, "Suppressed inotify event spam.");
							}

						}
					}
				}
			}
		}
	}

	syslog(LOG_NOTICE, "Shutting down daemon...");
	for (int i = 0; i < g_active_proc; i++)
	{
		if (watch_table[i].pid > 0)
		{

			const char *curr_proc_name = watch_table[i].name;
			pid_t curr_proc_pid = watch_table[i].pid;
			syslog(LOG_NOTICE, "Shutting down process: %s PID %d", curr_proc_name, curr_proc_pid);			
			kill(watch_table[i].pid, SIGTERM);
			// waitpid(watch_table[i].pid, NULL, 0);			
			struct rusage usage;
			int status;
			if (wait4(curr_proc_pid, &status, 0, &usage) != -1)
			{
				syslog(LOG_INFO, "%s Statistics:", curr_proc_name);
				syslog(LOG_INFO, "RAM Peak: %ld KB", usage.ru_maxrss);
				syslog(LOG_INFO, "User CPU Time: %ld.%06ld sec", 
					   usage.ru_utime.tv_sec, usage.ru_utime.tv_usec);
				syslog(LOG_INFO, "System CPU Time: %ld.%06ld sec", 
					   usage.ru_stime.tv_sec, usage.ru_stime.tv_usec);
				syslog(LOG_INFO, "Voluntary Context Switches: %ld", usage.ru_nvcsw);
			}
		}
		free((void*)watch_table[i].name);
		free((void*)watch_table[i].path);
		free((void*)watch_table[i].config);
	}
	free(watch_table);
	watch_table = NULL;
	close(timer_fd);
	close(inotify_fd);
	close(epoll_fd);
	syslog(LOG_NOTICE, "Daemon vanished. All children reaped");
	closelog();

	return EXIT_SUCCESS;	
}

void daemon_reload_config(const char *full_path, const char *prj_path)	
{
	char *json_data = daemon_read_conf(full_path);
	if (!json_data)
	{
		syslog(LOG_ERR, "daemon_read_conf failed.");
		return;
	}
	cJSON *root = cJSON_Parse(json_data);
	if (!root)
	{
		syslog(LOG_ERR, "cJSON_Parse failed.");
		free(json_data);
		return;
	}
	cJSON *modules = cJSON_GetObjectItemCaseSensitive(root, "modules");
	if (!cJSON_IsArray(modules))
	{
		cJSON_Delete(root);
		free(json_data);
		return;
	}
	int new_count = cJSON_GetArraySize(modules);
	watch_entry_t *new_table = calloc(new_count, sizeof(watch_entry_t));
	if (!new_table)
	{
		syslog(LOG_ERR, "calloc failed.");
		cJSON_Delete(root);
		free(json_data);
		exit(EXIT_FAILURE);
	}
	cJSON *module = NULL;
	int i = 0;
	cJSON_ArrayForEach(module, modules)
	{
		cJSON *n = cJSON_GetObjectItemCaseSensitive(module, "name");
		cJSON *p = cJSON_GetObjectItemCaseSensitive(module, "bin_path");
		cJSON *h = cJSON_GetObjectItemCaseSensitive(module, "heartbeat_interval");
		if (cJSON_IsString(n) && cJSON_IsString(p) && cJSON_IsNumber(h))
		{
			new_table[i].name   = strdup(n->valuestring);
			new_table[i].path   = strdup(p->valuestring);
			new_table[i].heartbeat_speed = h->valueint;
			new_table[i].config = cJSON_PrintUnformatted(module);
			new_table[i].pid    = 0;
			new_table[i].alive  = 0;
			i++;
		}
		else
		{
			syslog(LOG_WARNING, "Module entry %d is missing", i);
		}		
	}
	if (i == 0)
	{
		syslog(LOG_ERR, "No valid module found in config.");
	}
	/* block signals while reloading */
	sigset_t mask, oldmask;
	sigemptyset(&mask); /* clear flags */
	sigaddset(&mask, HEARTBEAT_SIG); /* set flags */
	sigprocmask(SIG_BLOCK, &mask, &oldmask); /* Tell kernel to block my mask, save curr state into oldmask */
	if (watch_table)
	{
		/* transfer existing proc if name and path already exist */
		for (int j = 0; j < i; j++)
		{
			for (int k = 0; k < g_active_proc; k++)
			{		
				if (watch_table[k].pid > 0 &&
					strcmp(new_table[j].name, watch_table[k].name) == 0 &&
					strcmp(new_table[j].path, watch_table[k].path) == 0)
				{
					new_table[j].pid = watch_table[k].pid;
					new_table[j].alive = 1;
					watch_table[k].pid = 0;
					break;
				}
			}
		}
		/* clean up removed processes */
		for (int j = 0; j < g_active_proc; j++)
		{
			if (watch_table[j].pid > 0)
			{
				syslog(LOG_NOTICE, "Process: %s removed. Killing PID: %d",
					   watch_table[j].name, watch_table[j].pid);
				kill(watch_table[j].pid, SIGTERM);
				waitpid(watch_table[j].pid, NULL, WNOHANG);
			}
			free((void*)watch_table[j].name);
			free((void*)watch_table[j].path);
			free((void*)watch_table[j].config);
		}
		free(watch_table);		
	}
	watch_table = new_table;
	g_active_proc = i;
	/* restore old signal mask */
	sigprocmask(SIG_SETMASK, &oldmask, NULL);
	/* spawn new process that wasn't preserved */
	for (int j = 0; j < g_active_proc; j++)
	{
		if (watch_table[j].pid == 0)
		{
			daemon_spawn_process(j, watch_table[j].path, watch_table[j].name,
								 watch_table[j].heartbeat_speed, prj_path);
		}
	}
	cJSON_Delete(root);
	free(json_data);
}

void daemon_perform_health_check(const char *prj_path)
{
	for (int i = 0; i < g_active_proc; i++)
	{
		int status;
		pid_t rv = waitpid(watch_table[i].pid, &status, WNOHANG);
		if (rv > 0)
		{
			syslog(LOG_ERR, "Process: %s PID: %d terminated. Restarting.",
				   watch_table[i].name, watch_table[i].pid);
			daemon_spawn_process(i, watch_table[i].path, watch_table[i].name,
				                 watch_table[i].heartbeat_speed, prj_path);
		}
		else if (!watch_table[i].alive)
		{
			syslog(LOG_ERR, "Process: %s PID: %d hung. Restarting.",
				   watch_table[i].name, watch_table[i].pid);
			kill(watch_table[i].pid, SIGKILL);
			waitpid(watch_table[i].pid, NULL, 0);
			daemon_spawn_process(i, watch_table[i].path, watch_table[i].name,
				                 watch_table[i].heartbeat_speed, prj_path);			
		}
		else
		{
			/* Heartbeat received */
			syslog(LOG_INFO, "Process: %s PID: %d is OK", watch_table[i].name,
				   watch_table[i].pid);
			watch_table[i].alive = 0;
		}
	}
}

void daemon_spawn_process(int idx, const char *path, const char *name, int speed, const char *wd)
{
	int pipefd[2];
	if (pipe2(pipefd, O_CLOEXEC) == -1)
	{
		syslog(LOG_ERR, "daemon_spawn_process: pipe failed");
		return;
	}
    pid_t p = fork();
    if (p < 0)
    {
		close(pipefd[0]);
		close(pipefd[1]);
		syslog(LOG_ERR, "daemon_spawn_process: fork failed");
        return;
    }
    if (p == 0)
    {		
        /* CHILD CONTEXT */
		close(pipefd[0]);
		if (chdir(wd) != 0)
		{
			syslog(LOG_CRIT, "FATAL ERROR: failed to change directory");
			_exit(EXIT_FAILURE);
		}
		if (watch_table[idx].config != NULL)
		{
			setenv("SUNSPOTS_CONFIG", watch_table[idx].config, 1);
		}
		char p_pid_str[16];
        char p_hspeed_str[16];
		sprintf(p_pid_str, "%d", getppid());
		sprintf(p_hspeed_str, "%d", speed);
        char *args[] = { (char*)path, p_pid_str, p_hspeed_str, NULL };        
        execvp(args[0], args);
		int err = errno;
		write(pipefd[1], &err, sizeof(err));		
        _exit(EXIT_FAILURE);
    }
	/* PARENT CONTEXT */
	close(pipefd[1]);
	int err = 0;
	if (read(pipefd[0], &err, sizeof(err)) > 0)
	{
		syslog(LOG_ERR, "execvp failed. Process: %s. Error: %s", name, strerror(err));
		watch_table[idx].pid = -1;
	}
	else
	{
		watch_table[idx].pid = p;
		watch_table[idx].alive = 1;
		syslog(LOG_INFO, "Spawned process: %s PID: %d", name, p);
	}
	close(pipefd[0]);   
}

void daemon_heartbeat_handler(int sig, siginfo_t *info, void *context)
{
    pid_t sender = info->si_pid;
    for (int i = 0; i < g_active_proc; i++)
    {
        if (watch_table && watch_table[i].pid == sender)
        {
            watch_table[i].alive = 1;
            return;
        }
    }
}

void daemon_signal_setup()
{
	struct sigaction sa_rt;
	memset(&sa_rt, 0, sizeof(sa_rt));
	sa_rt.sa_sigaction = daemon_heartbeat_handler;
	sa_rt.sa_flags     = SA_SIGINFO | SA_RESTART;
	sigemptyset(&sa_rt.sa_mask);
	if (sigaction(HEARTBEAT_SIG, &sa_rt, NULL) != 0)
	{
		syslog(LOG_CRIT, "FATAL ERROR: failed to register hearbeat signal %d: %m", HEARTBEAT_SIG);
		exit(EXIT_FAILURE);
	}
	struct sigaction sa_exit;
	memset(&sa_exit, 0, sizeof(sa_exit));
	sa_exit.sa_handler = daemon_shutdown_handler;
	sigemptyset(&sa_exit.sa_mask);
	if (sigaction(SIGTERM, &sa_exit, NULL) != 0)
	{
		syslog(LOG_CRIT, "FATAL ERROR: failed to register SIGTERM: %m");
		exit(EXIT_FAILURE);
	}
	if (sigaction(SIGINT, &sa_exit, NULL) != 0)
	{
		syslog(LOG_CRIT, "FATAL ERROR: failed to register SIGINT: %m");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Successfully setup signal handling.");
}

void daemon_shutdown_handler(int sig)
{
    syslog(LOG_INFO,"Daemon killed.");
    g_daemon_running = 0;
}

char *daemon_read_conf(const char *filepath)
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

void daemon_set_env(char **config)
{
	if (setenv("SUNSPOTS_CONFIG", *config, 1) != 0)
	{
		syslog(LOG_ERR, "Could not load config into environment.");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Loaded config into environment.");
}
