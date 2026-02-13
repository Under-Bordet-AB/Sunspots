/**
 * SUNSPOTS daemon
 * Uses a flat model.
 * Hot re-load if user updates config.
 * TO-DO: PID lock?
 **/

#define _GNU_SOURCE
#include "daemon.h"
#define CONFIG_FILENAME "sunspots.json"

watch_entry_t *watch_table = NULL;
int g_active_proc = 0;
char g_prj_root[MAX_PATH];
volatile sig_atomic_t g_daemon_running = 1;

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
    /* if (argc < 2 || strcmp(argv[1], "daemon") != 0)		 */
    /* { */
    /*     printf(" !! Usage: %s daemon\n", argv[0]); */
    /*     return EXIT_FAILURE; */
    /* } */

	/**
	 * DAEMON SETUP
	 **/

	/** resolve root path for project, path to config file and directory */
	daemon_resolve_project_root();
	char config_path[PATH_MAX];
	if (snprintf(config_path, sizeof(config_path), "%s/config/%s", g_prj_root, CONFIG_FILENAME) == -1)
	{
		perror("snprintf failed");
		return EXIT_FAILURE;
	}
	if (access(config_path, R_OK) != 0)
	{
		fprintf(stderr, " !! FATAL; Config file missing or unreadable at %s\n", config_path);
		return EXIT_FAILURE;
	}
	char config_dir[PATH_MAX];
	if (snprintf(config_dir, sizeof(config_dir), "%s/config", g_prj_root) == -1)
	{
		syslog(LOG_CRIT, "snprintf failed: %m");
		exit(EXIT_FAILURE);
	}
	
	/** run daemon macro, open log and setup signal handling */
	DAEMONIZE();
	openlog("SUNSPOTS_DAEMON", LOG_PID, LOG_DAEMON);
	syslog(LOG_NOTICE, "Sunspots daemon started successfully. Detached and darkened.");	
    daemon_signal_setup();	

	/** setup epoll, inotify and timerfd */
	int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (epoll_fd == -1)
	{
		syslog(LOG_ERR, "epoll_create1 failed: %m");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Sucessfully setup epoll");

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

	int global_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (global_timer_fd == -1)
	{
		syslog(LOG_ERR, "timerfd_create failed: %m");
		exit(EXIT_FAILURE);
	}
	struct itimerspec ts =
	{
		.it_interval = { HEALTH_CHECKUP_INTERVAL, 0 },
		.it_value    = { HEALTH_CHECKUP_INTERVAL, 0 }
	};
	if (timerfd_settime(global_timer_fd, 0, &ts, NULL) == -1)
	{
		syslog(LOG_ERR, "timerfd_settime failed: %m");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Successfully setup timerfd");

	/** register core fds with epoll */
	struct epoll_event ev;
	struct epoll_event events[MAX_EVENTS];
	ev.events  = EPOLLIN;
	ev.data.fd = global_timer_fd;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD , global_timer_fd, &ev) == -1)
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

	daemon_load_modules(config_path, g_prj_root, epoll_fd);

	/**
	 * MONITORING LOOP
	 **/

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
			/* global timerfd triggered: health checkup*/
			if (events[i].data.fd == global_timer_fd)
			{
				uint64_t exp;
				if (read(global_timer_fd, &exp, sizeof(exp)) > 0)
				{
					daemon_perform_health_check(g_prj_root);
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
								daemon_load_modules(config_path, g_prj_root, epoll_fd);
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
			else
			{
				/** Chrono-mon task */
				for (int j = 0; j < g_active_proc; j++)
				{
					if (events[j].data.fd == watch_table[j].timer_fd)
					{
						uint64_t exp;
						if ((read(watch_table[j].timer_fd, &exp, sizeof(exp))) > 0)
						{
							if (watch_table[j].pid <= 0)
							{
								syslog(LOG_NOTICE, "Timer triggered for %s", watch_table[j].name);
								daemon_spawn_process(j, g_prj_root);
							}
							if (watch_table[j].timertype == MODE_ABSTIME)
							{
								syslog(LOG_INFO, "Recalculate abs-time for: %s", watch_table[j].name);
								daemon_module_timer_config(&watch_table[j], epoll_fd);
							}
							else
							{
								syslog(LOG_ERR, "Task: %s skipped, already running.", watch_table[j].name);
							}
						}
					}
				}
			}
		}
	}

	/**
	 * SHUTDOWN
	 **/
	
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
	close(global_timer_fd);
	close(inotify_fd);
	close(epoll_fd);
	syslog(LOG_NOTICE, "Daemon vanished. All children reaped");
	closelog();

	return EXIT_SUCCESS;	
}


/**
 * FUNCTIONS
 **/

void daemon_load_modules(const char *config_path, const char *prj_root_path, int epoll_fd)	
{
	char *json_data = daemon_read_conf(config_path);
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
		cJSON *n      = cJSON_GetObjectItemCaseSensitive(module, "name");
		cJSON *p      = cJSON_GetObjectItemCaseSensitive(module, "bin_path");
		/** saftey check, if we don't have name or path, go to top */
		if (!cJSON_IsString(n) || !cJSON_IsString(p)) continue;
		new_table[i].name = strdup(n->valuestring);
		char raw_path[PATH_MAX];
		if (snprintf(raw_path, sizeof(raw_path), "%s/%s", prj_root_path, p->valuestring) == -1)
		{
			syslog(LOG_CRIT, "snprintf failed: %m");
			exit(EXIT_FAILURE);
		}
		char *resolved_path = realpath(raw_path, NULL);
		if (resolved_path)
		{
			new_table[i].path = resolved_path;
		}
		new_table[i].config = cJSON_PrintUnformatted(module);
		new_table[i].timer_fd = -1;

		cJSON *t_flag = cJSON_GetObjectItemCaseSensitive(module, "Timer-type");
		if (cJSON_IsNumber(t_flag) && t_flag->valueint == 1)
		{
			cJSON *t_abs = cJSON_GetObjectItemCaseSensitive(module, "Abs-time");
			if (cJSON_IsString(t_abs))
			{
				new_table[i].timertype = MODE_ABSTIME;
				new_table[i].abs_target_time = strdup(t_abs->valuestring);
			}
			else
			{
				cJSON *t_rel = cJSON_GetObjectItemCaseSensitive(module, "Rel-time");
				new_table[i].timertype = MODE_RELTIME;
				new_table[i].relative_target_time = cJSON_IsNumber(t_rel) ? t_rel->valueint : ONE_MINUTE;
			}
		}
		else
		{
			new_table[i].timertype = MODE_HEARTBEAT;
			cJSON *h = cJSON_GetObjectItemCaseSensitive(module, "heartbeat_interval");
			new_table[i].heartbeat_speed = cJSON_IsNumber(h) ? h->valueint : HEARTBEAT_SPEED;
		}
		i++;
	}

	/** block signals while reloading */
	sigset_t mask, oldmask;
	sigemptyset(&mask); /* clear flags */
	sigaddset(&mask, HEARTBEAT_SIG); /* set flags */
	sigprocmask(SIG_BLOCK, &mask, &oldmask); /* Tell kernel to block my mask, save curr state into oldmask */
	if (watch_table)
	{
		/** transfer existing proc if name and path already exist */
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
					new_table[j].timer_fd = watch_table[k].timer_fd;
					watch_table[k].pid = 0;
					watch_table[k].timer_fd = -1;
					break;
				}
			}
		}
		/** clean up removed processes */
		for (int j = 0; j < g_active_proc; j++)
		{
			if (watch_table[j].pid > 0)
			{
				syslog(LOG_NOTICE, "Process: %s removed. Killing PID: %d", watch_table[j].name, watch_table[j].pid);
				kill(watch_table[j].pid, SIGTERM);
				waitpid(watch_table[j].pid, NULL, WNOHANG);
			}
			if (watch_table[j].timer_fd > 0)
			{
				epoll_ctl(epoll_fd, EPOLL_CTL_DEL, watch_table[j].timer_fd, NULL);
				close(watch_table[j].timer_fd);
			}

			free((void*)watch_table[j].name);
			free((void*)watch_table[j].path);
			free((void*)watch_table[j].config);
			if (watch_table[j].abs_target_time)
			{
				free(watch_table[j].abs_target_time);
			}
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
		if (watch_table[j].timertype != MODE_HEARTBEAT && watch_table[j].timer_fd == -1)
		{
			daemon_module_timer_config(&watch_table[j], epoll_fd);
		}
		if (watch_table[j].timertype == MODE_HEARTBEAT && watch_table[j].pid == 0)
		{
			daemon_spawn_process(j, prj_root_path);
		}
	}
	cJSON_Delete(root);
	free(json_data);
}

void daemon_module_timer_config(watch_entry_t *module, int epoll_fd)
{
	if (module->timertype == MODE_HEARTBEAT) return;
	struct itimerspec its = {0};
	int clock_type = 0;
	int timerfd_flags = 0;
	if (module->timertype == MODE_ABSTIME)
	{
		clock_type = CLOCK_REALTIME;
	}
	else
	{
		clock_type = CLOCK_MONOTONIC;
	}
	if (module->timer_fd <= 0)
	{
		module->timer_fd = timerfd_create(clock_type, TFD_NONBLOCK | TFD_CLOEXEC);
		if (module->timer_fd == -1)
		{
			syslog(LOG_ERR, "timerfd_create failed for %s: %m", module->name);
			return;
		}
		struct epoll_event ev =
		{
			.events  = EPOLLIN,
			.data.fd = module->timer_fd
		};
		/** register timer with epoll */
		epoll_ctl(epoll_fd, EPOLL_CTL_ADD, module->timer_fd, &ev);
	}
	/** Absolute time */
	if (module->timertype == MODE_ABSTIME)
	{
		int hr;
		int min;
		if (sscanf(module->abs_target_time, "%d:%d", &hr, &min) != 2)
		{
			syslog(LOG_ERR, "Unexpected timeformat for timer");
			return;
		}
		struct timespec current_time;
		if ((clock_gettime(clock_type, &current_time)) != 0)
		{
			syslog(LOG_ERR, "clock_gettime failed: %m");
			return;
		}
		/** localtime() uses global buffer, safer with localtime_r(), therefor need buffer */
		struct tm tm_buf;
		struct tm *lt = localtime_r(&current_time.tv_sec, &tm_buf);
		lt->tm_hour  = hr;
		lt->tm_min   = min;
		lt->tm_sec   = 0;
		/** auto detect daylight savings */
		lt->tm_isdst = -1; 
		time_t run_at = mktime(lt);
		/** did abs target time already happen? */
		if (run_at <= current_time.tv_sec)
		{
			/** calender math, skip a day */
			lt->tm_mday += 1;
			/** recalculate and let mktime handle leap year bull-shit */
			run_at = mktime(lt);
		}
		its.it_value.tv_sec = run_at;
		its.it_value.tv_nsec = 0;
		its.it_interval.tv_sec = 0;		
		timerfd_flags = TFD_TIMER_ABSTIME;
	}
	/* Relative time */
	else
	{
		its.it_value.tv_sec = module->relative_target_time;
		its.it_interval.tv_sec = module->relative_target_time;
	}
	if (timerfd_settime(module->timer_fd, timerfd_flags, &its, NULL) == -1)
	{
		syslog(LOG_ERR, "timerfd_settime failed for %s: %m", module->name);
	}	
}

void daemon_spawn_process(int idx, const char *prj_root_path)
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
		if (chdir(prj_root_path) != 0)
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
		sprintf(p_hspeed_str, "%d", watch_table[idx].heartbeat_speed);
        char *args[] = { (char*)watch_table[idx].path, p_pid_str, p_hspeed_str, NULL };        
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
		syslog(LOG_ERR, "execvp failed. Error: %s", strerror(err));
		watch_table[idx].pid = -1;
	}
	else
	{
		watch_table[idx].pid = p;
		watch_table[idx].alive = 1;
		syslog(LOG_INFO, "Spawned process PID: %d", p);
	}
	close(pipefd[0]);   
}

void daemon_perform_health_check(const char *prj_root_path)
{
	for (int i = 0; i < g_active_proc; i++)
	{
		if (watch_table[i].timertype != MODE_HEARTBEAT) continue;
		if (watch_table[i].pid <= 0)
		{
			syslog(LOG_ERR, "Process: %s PID: %d terminated. Restarting.", watch_table[i].name, watch_table[i].pid);
			daemon_spawn_process(i, prj_root_path);
		}
		else if (!watch_table[i].alive)
		{			
			syslog(LOG_ERR, "Process: %s PID: %d hung. Restarting.", watch_table[i].name, watch_table[i].pid);
			kill(watch_table[i].pid, SIGKILL);
			waitpid(watch_table[i].pid, NULL, 0);
			watch_table[i].pid = 0;
			daemon_spawn_process(i, prj_root_path);
		}
		else
		{
			syslog(LOG_INFO, "Process: %s PID: %d is OK", watch_table[i].name, watch_table[i].pid);
			watch_table[i].alive = 0;
		}
	}
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

void daemon_resolve_project_root()
{
	/* readlink()  places  the  contents of the symbolic link pathname in the buffer
	 * /proc/self/exe: A special symlink in Linux that points to the actual executable
	 * file of the current process.
	 */
	char exe_path[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path));
	if (len == -1)
	{
		perror(" !! FATAL: Cannot read /proc/self/exe");
		exit(EXIT_FAILURE);
	}
	exe_path[len] = '\0';
	/* add directory of binary */
	char *bin_dir = dirname(exe_path);
	char path_to_root[PATH_MAX];
	snprintf(path_to_root, sizeof(path_to_root), "%s/../..", bin_dir);
	/* realpath() expands all symbolic links and resolves references to /./, /../
	 * and extra '/' characters in the null-terminated string named by path to produce
	 * a canonicalized absolute pathname.
	 */
	if (realpath(path_to_root, g_prj_root) == NULL)
	{
		fprintf(stderr," !! FATAL: Could not resolve project root from %s: %s\n",
				path_to_root, strerror(errno));
	}
}

/** Signal handling */

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

void daemon_sigchld_handler(int sig)
{
	(void)sig;
	int saved_errno = errno;
	int status;
	pid_t pid;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
	{
		if (watch_table)
		{
			for (int i = 0; i < g_active_proc; i++)
			{
				if (watch_table[i].pid == pid)
				{
					watch_table[i].pid = 0;
					watch_table[i].alive = 0;
					break;
				}
			}
		}
	}
}

void daemon_shutdown_handler(int sig)
{
    syslog(LOG_INFO,"Daemon killed.");
    g_daemon_running = 0;
}

void daemon_signal_setup()
{
	struct sigaction sa;
    // 1. Initialize to zeros
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);

    sa.sa_sigaction = daemon_heartbeat_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(HEARTBEAT_SIG, &sa, NULL);

    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; // Implicitly clears SA_SIGINFO
    sa.sa_handler = daemon_sigchld_handler;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_flags = 0;
    sa.sa_handler = daemon_shutdown_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
}
