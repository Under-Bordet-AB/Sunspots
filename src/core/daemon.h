/**
 * @file daemon.h
 * @brief Signal handling and structure definitions for the Sunspots watchdog.
 * * This module manages the flat process model, overseeing the health of 
 * forked workers and handling the system-level signals required to 
 * keep the supervisor running independently of a shell.
 */

#ifndef DAEMON_H
#define DAEMON_H


#define DAEMONIZE() \
do { \
    pid_t init_pid = fork(); \
    if (init_pid < 0) { perror(" !! 1st fork failed"); exit(EXIT_FAILURE); } \
    if (init_pid > 0) { printf(" >> Daemon is now running in the background.\n"); exit(EXIT_SUCCESS);} \
    else { if (setsid() < 0) { perror(" !! setsid failed"); exit(EXIT_FAILURE); } \
    pid_t second_pid = fork(); \
    if (second_pid < 0) { perror(" !! 2th fork failed"); exit(EXIT_FAILURE); } \
    else if (second_pid > 0) { exit(EXIT_SUCCESS); } \
    else {  printf(" >> Daemon cannot be re-aqcuired by terminal.\n" \
                   " >> Daemon is now entering the dark and empty void.\n" \
                   " !! USE 'kill -SIGINT %i' TO KILL IT!\n", getpid()); \
    umask(0); \
    if (chdir("/") != 0) { exit(EXIT_FAILURE); } \
    close(STDIN_FILENO); close(STDOUT_FILENO); close(STDERR_FILENO); \
    int scream_into_the_void = open("/dev/null", O_RDWR); \
    dup2(scream_into_the_void, STDIN_FILENO); dup2(scream_into_the_void, STDOUT_FILENO); \
    dup2(scream_into_the_void, STDERR_FILENO);} } \
} while (0)

#include "../../src/libs/json/cJSON.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <limits.h>
#include <libgen.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/timerfd.h>

#define MAX_PATH 512
#define MAX_EVENTS 10
#define HEALTH_CHECKUP_INTERVAL 5
/** acts as default for heartbeats */
#define HEARTBEAT_SPEED 2
#define DAY_IN_SEC 86400
/** acts as default for rel-time */
#define ONE_MINUTE 60
#define HEARTBEAT_SIG SIGRTMIN

typedef enum{
	MODE_HEARTBEAT = 0,
	MODE_RELTIME,
	MODE_ABSTIME
} module_timer_mode_t;

typedef struct watch_entry
{
	/** Module specific */
    pid_t pid;                      /* PID of process */
	const char* name;               /* Name for logs etc */
    const char* path;               /* Binary path  */
	const char* config;             /* JSON config blob */
	/** Timer specific **/
	module_timer_mode_t timertype;  /* Enum describing type */
	char *abs_target_time;    /* Absolute target */
	long relative_target_time;      /* Time interval to next run */
	int timer_fd;               	/* Timer filedescriptor  */
	/** Heartbeat specific **/
	int heartbeat_speed;            /* Heartbeat signal speed */
    volatile sig_atomic_t alive;    /* Flag for alive or dead */			
} watch_entry_t;


extern watch_entry_t *watch_table;
extern int g_active_proc;
extern volatile sig_atomic_t g_daemon_running;
extern char g_prj_root[];

void daemon_signal_setup();
void daemon_heartbeat_handler(int sig, siginfo_t *info, void *context);
void deamon_sigchld_handler(int sig);
void daemon_shutdown_handler(int sig);

void daemon_reload_config(const char *full_path, const char *prj_path, int epoll_fd);
void daemon_module_timer_config(watch_entry_t *module, int epoll_fd);
char *daemon_read_conf(const char *filepath);
void daemon_spawn_process(int idx, const char *prj_path);
void daemon_perform_health_check(const char *prj_path);
void daemon_resolve_project_root();

#endif /* DAEMON_H */
