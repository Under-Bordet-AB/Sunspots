/**
 * @file daemon.h
 */

#ifndef DAEMON_H
#define DAEMON_H

#include <signal.h>
#include <linux/limits.h>

#define CONFIG_FILENAME         "sunspots.json"
#define MAX_EVENTS              10
#define HEALTH_CHECKUP_INTERVAL 5
#define HEARTBEAT_SPEED         2
#define ONE_MINUTE              60
#define HEARTBEAT_SIG           SIGRTMIN
#define HEARTBEAT_OFFSET        4

typedef struct daemon_var daemon_var_t;

/**
 * @brief Fully initializes the daemon and its internal state.
 * * @details This function allocates memory for the daemon context, resolves the
 * absolute paths to the project root and configuration directory, daemonizes the
 * process (detaching from the controlling terminal unless in DEBUG mode), sets up
 * epoll/signalfd/timerfd/inotify, loads the initial modules, and writes the PID file.
 * * @param self_ptr A pointer to the daemon context pointer. Will be allocated and populated.
 * If allocation or critical setup fails, the function will call exit(EXIT_FAILURE).
 */
void daemon_init(daemon_var_t **self_ptr);

/**
 * @brief Starts the primary epoll event loop.
 * * @details The loop is highly efficient; rather than actively polling, the daemon
 * sleeps when idle. It consumes effectively zero CPU until the kernel wakes it up
 * to process a specific event (such as a timer expiring, a child process signal,
 * or a configuration file change). The loop runs continuously until a termination
 * signal (SIGTERM/SIGINT) is received.
 * * @param self Pointer to the initialized daemon context.
 */
void daemon_run(daemon_var_t *self);

/**
 * @brief Performs a graceful shutdown of the daemon.
 * * @details Safely terminates all running child modules, closes all open file
 * descriptors (epoll, timers, inotify, signals), frees allocated memory, and
 * closes the syslog connection.
 * * @param self_ptr Pointer to the daemon context pointer. The memory will be freed
 * and the pointer will be set to NULL.
 */
void daemon_deinit(daemon_var_t **self_ptr);

#endif /* DAEMON_H */
