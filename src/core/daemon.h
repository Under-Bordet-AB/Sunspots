/**
 * @file daemon.h
 * @brief Signal handling and structure definitions for the Sunspots watchdog.
 * * This module manages the flat process model, overseeing the health of 
 * forked workers and handling the system-level signals required to 
 * keep the supervisor running independently of a shell.
 */

#ifndef DAEMON_H
#define DAEMON_H

/*
 * DAEMONIZE() - Detach process from terminal and run as daemon
 *
 * This macro performs the double-fork technique to properly daemonize a process:
 * 1. First fork() - parent exits, child continues in background
 * 2. setsid() - child becomes session leader, detaches from controlling terminal
 * 3. Second fork() - ensures process can never reacquire a controlling terminal
 * 4. Cleanup - set umask, chdir to /, redirect stdio to /dev/null
 *
 * NOTE: Wrapped in do-while(0) to ensure macro behaves as a single statement.
 * This prevents bugs when used in if/else chains or other control flow contexts.
 * Example of the problem without do-while(0):
 *
 *   if (condition)
 *       DAEMONIZE();  // Multiple statements without braces
 *   else
 *       other_code(); // Would bind to wrong 'if', causing compile error
 *
 * The do-while(0) forces the macro to be a single compound statement that
 * requires a semicolon, making it behave like a function call.
 */
#define DAEMONIZE() \
do { \
    /* THE DAEMON DETACHMENT RITUAL */ \
    /* Step 1: First fork to get our child to run in the background */ \
    pid_t init_pid = fork(); \
    if (init_pid < 0) \
    { \
        perror(" !! 1st fork failed"); \
        exit(EXIT_FAILURE); \
    } \
    if (init_pid > 0) \
    { \
        printf(" >> Daemon is now running in the background.\n"); \
        exit(EXIT_SUCCESS); \
    } \
    /* Step 2: We are now the first child. Become session leader, closing the shell won't kill us now */ \
    else \
    { \
        if (setsid() < 0) \
        { \
            perror(" !! setsid failed"); \
            exit(EXIT_FAILURE); \
        } \
        pid_t second_pid = fork(); \
        if (second_pid < 0) \
        { \
            perror(" !! 2th fork failed"); \
            exit(EXIT_FAILURE); \
        } \
        else if (second_pid > 0) \
        { \
            /* Step 3: Our child is not a session leader and can not be re-acquired by a terminal. */ \
            exit(EXIT_SUCCESS); \
        } \
        /* Step 4: Send the daemon into the void */ \
        else \
        { \
            printf(" >> Daemon cannot be re-aqcuired by terminal.\n" \
                   " >> Daemon is now entering the dark and empty void.\n" \
                   " !! USE 'kill -SIGINT %i' TO KILL IT!\n", getpid()); \
            umask(0);                       /* Daemon has many rights */ \
            if (chdir("/") != 0)            /* Change wd to root */ \
            { \
                perror(" !! chdir failed"); \
                exit(EXIT_FAILURE); \
            } \
            close(STDIN_FILENO); \
            close(STDOUT_FILENO); \
            close(STDERR_FILENO); \
            int scream_into_the_void = open("/dev/null", O_RDWR); \
            dup2(scream_into_the_void, STDIN_FILENO); \
            dup2(scream_into_the_void, STDOUT_FILENO); \
            dup2(scream_into_the_void, STDERR_FILENO); \
        } \
    } \
} while (0)

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/** @brief Maximum number of child processes the supervisor can track. */
#define MAX_CHILDREN 4

/** @brief The frequency (seconds) at which the daemon inspects the watch table. */
#define HEALTH_CHECKUP_INTERVAL 5

/** @brief Default pulse rate for children if no specific speed is provided. */
#define HEARTBEAT_SPEED 2

/** * @brief Real-Time signal (SIGRTMIN) used for heartbeats.
 * Real-time signals are queued by the kernel, preventing lost pulses.
 */
#define HEARTBEAT_SIG SIGRTMIN

/**
 * @struct watch_entry
 * @brief The "Medical Chart" for a monitored process.
 */
typedef struct watch_entry
{
    pid_t pid;                      /**< The OS-assigned Process ID. */
	const char* name;               /**< Human-readable label for logs. */
    const char* path;               /**< Full path to the worker binary. */
    int heartbeat_speed;            /**< Expected pulse frequency. */
	const char* config;
    volatile sig_atomic_t alive;    /**< 1 if a pulse was received, 0 otherwise. */
} watch_entry_t;

/** @brief Shared table of all monitored child processes. */
extern watch_entry_t *watch_table;

/** @brief The number of slots currently occupied in the watch_table. */
extern int active_processes;

/** @brief Global flag to control the daemon's lifecycle. */
extern volatile sig_atomic_t g_daemon_running;

/**
 * @brief Signal handler that identifies a worker pulse via its PID.
 * @param sig The signal number (HEARTBEAT_SIG).
 * @param info Kernel-provided info containing the sender's PID.
 * @param context Unused signal context.
 */
void heartbeat_handler(int sig, siginfo_t *info, void *context);

/**
 * @brief Standard termination handler to trigger a graceful shutdown.
 * @param sig Signal received (SIGTERM or SIGINT).
 */
void daemon_shutdown_handler(int sig);

/**
 * @brief Sets up the signal infrastructure for the supervisor.
 */
void daemon_signal_setup();

/**
 * @brief Spawns a new worker by forking and executing a separate binary.
 * @param index The slot in the watch_table to occupy.
 * @param path Path to the executable.
 * @param name Friendly name for the process.
 * @param speed Frequency of heartbeats expected from this child.
 */
void spawn_process(int index, const char *path, const char *name, int speed, const char *wd);

#endif /* DAEMON_H */
