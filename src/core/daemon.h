/**
 * @file daemon.h
 * @brief Signal handling and structure definitions for the Sunspots watchdog.
 * * This module manages the flat process model, overseeing the health of 
 * forked workers and handling the system-level signals required to 
 * keep the supervisor running independently of a shell.
 */

#ifndef DAEMON_H
#define DAEMON_H

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
    const char* path;               /**< Full path to the worker binary. */
    const char* name;               /**< Human-readable label for logs. */
    int heartbeat_speed;            /**< Expected pulse frequency. */
    volatile sig_atomic_t alive;    /**< 1 if a pulse was received, 0 otherwise. */
} watch_entry_t;

/** @brief Shared table of all monitored child processes. */
extern watch_entry_t watch_table[MAX_CHILDREN];

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
void spawn_process(int index, const char *path, const char *name, int speed);

#endif /* DAEMON_H */
