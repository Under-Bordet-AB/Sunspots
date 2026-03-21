/**
 * @file module.h
 */

#ifndef MODULE_H
#define MODULE_H

#include <sys/types.h>
#include <signal.h>

typedef struct module module_t;

/**
 * @brief Prepares the module handle.
 * @param self Pointer to the module handle to initialize.
 */
void module_init(module_t **self);

/**
 * @brief Cleans up all modules, kills processes, and frees memory.
 * * @details Sends SIGTERM to all active module PIDs, waits for them to exit
 * (logging their peak RAM usage), removes their timers from epoll, and frees
 * allocated strings and the array itself.
 * * @param self_ptr Pointer to the module array handle. Will be set to NULL.
 * @param count Number of modules currently in the array.
 * @param epoll_fd The epoll instance to remove timers from.
 */
void module_deinit(module_t **self_ptr, int count, int epoll_fd);

/**
 * @brief Parses the JSON config and populates/updates the module array.
 * * @details Reads the config file, allocates a new module array, and maps JSON
 * parameters (paths, timers, heartbeat intervals). It compares the new config
 * against the old one to perform seamless hot-reloading (only restarting modules
 * whose config changed).
 * * @param self_ptr Pointer to the current module array handle. Will be updated.
 * @param old_count Number of modules currently running prior to reload.
 * @param config_path Absolute path to the JSON configuration file.
 * @param prj_root_path Absolute path to the project root directory.
 * @param epoll_fd The main epoll file descriptor for attaching module timers.
 * @param heartbeat_sig The real-time signal used for heartbeats.
 * @return The new total number of modules loaded and tracked.
 */
int module_load(module_t **self_ptr, int old_count,  const char *config_path, 
                const char *prj_root_path, int epoll_fd, int heartbeat_sig);

/**
 * @brief Executes fork/execvp for a specific module.
 * * @details Sets up a pipe to catch `execvp` failures, forks the process, unblocks
 * standard signals in the child context, sets the environment variables
 * (SUNSPOTS_CONFIG, SUNSPOTS_SYSTEM, SUNSPOTS_SIGNAL), and executes the binary.
 * * @param self Pointer to the specific module to spawn.
 * @param prj_root_path Project root path to use as the child's working directory.
 * @param heartbeat_sig The signal the child should use to ping the parent.
 */
void module_spawn(module_t *self, const char *prj_root_path, int heartbeat_sig);

/**
 * @brief Checks if heartbeat modules are alive; restarts them if hung or dead.
 * * @details Iterates over all modules operating in MODE_HEARTBEAT. If a module has
 * unexpectedly terminated or failed to send a heartbeat signal (module_alive == 0),
 * it is forcefully killed (if hung) and respawned. Resets the `module_alive` flag.
 * * @param self Pointer to the module array.
 * @param count Number of modules in the array.
 * @param prj_root_path Project root path used for respawning.
 * @param heartbeat_sig The heartbeat signal to pass to respawned modules.
 */
void module_health_check_all(module_t *self, int count, const char *prj_root_path, int heartbeat_sig);

/**
 * @brief Identifies which module's timer triggered and executes it.
 * * @details Used when epoll detects activity on a timer_fd. Matches the FD to a
 * module and spawns its process.
 * * @note This function operates blindly for relative and absolute timers: it does
 * not check if a previous instance of the module is still running. It simply
 * spawns a new overlapping process every time the timer fires. If the module
 * uses an absolute timer, it also reconfigures the timer for its next occurrence.
 * * * @param self Pointer to the module array.
 * @param count Number of modules in the array.
 * @param timer_fd The file descriptor of the triggered timer.
 * @param prj_root Project root path used for spawning.
 * @param epoll_fd Epoll instance (needed if an absolute timer requires reconfiguration).
 * @param heartbeat_sig The signal to pass to the module.
 */
void module_handle_timer_event(module_t *self, int count, int timer_fd, 
                               const char *prj_root, int epoll_fd, int heartbeat_sig);

/**
 * @brief Configures the timerfd for a specific module (Rel-time or Abs-time).
 * * @details Creates a timerfd if one doesn't exist, attaches it to epoll, and
 * arms it based on the module's mode. Calculates the next trigger time using
 * CLOCK_REALTIME for absolute times (e.g., "14:30") or CLOCK_MONOTONIC for
 * relative intervals.
 * * @param self Pointer to the specific module.
 * @param epoll_fd Epoll instance to attach the new timerfd to.
 */
void module_timer_config(module_t *self, int epoll_fd);

/**
 * @brief Finds a module in an array based on its Process ID.
 * * @param self Pointer to the module array.
 * @param count Number of modules in the array.
 * @param pid The process ID to search for.
 * @return Pointer to the matched module_t, or NULL if not found.
 */
module_t *module_find_by_pid(module_t *self, int count, pid_t pid);

/**
 * @brief Retrieves the system configuration string shared among modules.
 * @param self Pointer to the module array (specifically uses self[0]).
 * @return A JSON-formatted string of the system config, or NULL.
 */
const char* module_get_system_config(module_t* self);

/**
 * @brief Retrieves the process ID of a specific module.
 * @param self Pointer to the module.
 * @return The process ID, or -1 if the module is invalid.
 */
pid_t module_get_pid(module_t* self);

/**
 * @brief Forcibly sets the process ID of a module.
 * @param self Pointer to the module.
 * @param value The new PID value.
 */
void module_set_pid(module_t* self, int value);

/**
 * @brief Updates the heartbeat 'alive' flag for a module.
 * @param self Pointer to the module.
 * @param value The alive status (usually 1 for alive, 0 for missing heartbeat).
 */
void  module_set_alive(module_t *self, int value);

#endif /* MODULE_H */
