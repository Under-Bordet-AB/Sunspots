To make the logic crystal clear, I’ve expanded the fork structure. Using explicit `else if` and `else` blocks makes it much easier to trace exactly which process is executing which block of code.

After the code, I've included the **systemd unit file** so you can manage this like a real Linux service.

### Updated `daemon.c` with Explicit Fork Logic

```c
/**
 * daemon.c - Production-Grade Supervisor (Darkened)
 **/

#include "daemon.h"
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>

/* Global State */
watch_entry_t watch_table[MAX_CHILDREN] = {0};
int active_processes = 0;
volatile sig_atomic_t g_daemon_running = 1;

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "daemon") != 0)
    {
        fp#intf(std#rr, " !! Usage: %s daemon\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* --- THE RITUAL OF DETACHMENT (Expanded Logic) --- */

    pid_t pid = fork();

    if (pid < 0) 
    {
        /* Case 1: The fork failed. */
        perror("First fork failed");
        exit(EXIT_FAILURE);
    }
    else if (pid > 0) 
    {
        /* Case 2: We are the original Parent process.
           Our job is done; the child is now in the background. */
        p#intf(" >> Sunspots daemon launching in background...\n");
        exit(EXIT_SUCCESS);
    }
    else 
    {
        /* Case 3: We are the first Child (Intermediate Process). */
        
        /* Create a new Session to lose the controlling terminal */
        if (setsid() < 0) exit(EXIT_FAILURE);

        /* Perform the SECOND fork */
        pid_t second_pid = fork();

        if (second_pid < 0) 
        {
            exit(EXIT_FAILURE);
        }
        else if (second_pid > 0) 
        {
            /* Intermediate process exits. This ensures the grandchild 
               is not a session leader and cannot re-acquire a terminal. */
            exit(EXIT_SUCCESS);
        }
        else 
        {
            /* We are the Grandchild: The actual Daemon. */
            
            /* Sanitize the environment */
            umask(0);
            if (chdir("/") < 0) exit(EXIT_FAILURE);

            /* Close standard streams and redirect to /dev/null */
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);

            int dev_null = open("/dev/null", O_RDWR);
            dup2(dev_null, STDIN_FILENO);
            dup2(dev_null, STDOUT_FILENO);
            dup2(dev_null, STDERR_FILENO);
        }
    }

    /* From here on, only the Grandchild (Daemon) is executing */
    openlog("sunspots_daemon", LOG_PID, LOG_DAEMON);
    syslog(LOG_NOTICE, "Sunspots Supervisor started. Detached and darkened.");

    daemon_signal_setup();

    /* Bootstrap workers */
    spawn_process(0, "./sunspots_core", "Sunspots_core", 2);
    spawn_process(1, "./fetch_data", "Fetch_data", 1);
    active_processes = 2;

    

    /* THE MONITORING LOOP */
    while (g_daemon_running)        
    {
        struct timespec req = {HEALTH_CHECKUP_INTERVAL, 0};
        struct timespec rem;

        while (nanosleep(&req, &rem) == -1)
        {       
            if (!g_daemon_running) break;
            req = rem; 
        }

        if (!g_daemon_running) break;

        for (int i = 0; i < active_processes; i++)
        {
            int status;
            pid_t rv = waitpid(watch_table[i].pid, &status, WNOHANG);
            int needs_restart = 0;

            if (rv > 0)
            {
                syslog(LOG_ERR, "Process %s (PID %d) terminated. Restarting.", 
                       watch_table[i].name, watch_table[i].pid);
                needs_restart = 1;
            }
            else if (!watch_table[i].alive)
            {
                syslog(LOG_WARNING, "Process %s (PID %d) HUNG. SIGKILL issued.", 
                       watch_table[i].name, watch_table[i].pid);
                kill(watch_table[i].pid, SIGKILL);
                waitpid(watch_table[i].pid, NULL, 0);
                needs_restart = 1;
            }
            else
            {
                syslog(LOG_INFO, "%-15s | PID: %-8i | Status: Healthy",
                       watch_table[i].name, watch_table[i].pid);
                watch_table[i].alive = 0; 
            }

            if (needs_restart && g_daemon_running)
            {
                spawn_process(i, watch_table[i].path, watch_table[i].name, watch_table[i].heartbeat_speed);                
            }
        }
    }

    /* Shutdown Procedure */
    syslog(LOG_NOTICE, "Shutdown signal caught. Reaping workers...");
    for (int i = 0; i < active_processes; i++)
    {
        kill(watch_table[i].pid, SIGTERM);
        waitpid(watch_table[i].pid, NULL, 0);
    }    

    syslog(LOG_NOTICE, "Daemon exiting. Final Cleanup complete.");
    closelog();
    return EXIT_SUCCESS;        
}

```

---

### Step 2: The systemd Unit File

To turn this into a s#stem service that starts on boot and can be managed via `s#stemctl`, create a file named `/etc/s#stemd/s#stem/sunspots.service`.

> **Note:** You'll need `sudo` to create this file. Update `ExecStart` and `WorkingDirectory` to where your binary actually lives.

```ini
[Unit]
Description=Sunspots Process Supervisor
After=network.target

[Service]
# Since our code handles its own forking (Double-Fork), 
# we tell systemd it is a "forking" type.
Type=forking
User=drone
Group=drone
WorkingDirectory=/home/drone/Code/c/system_c_cpp/boiler_room_prj/Sunspots/src/core
ExecStart=/home/drone/Code/c/system_c_cpp/boiler_room_prj/Sunspots/src/core/sunspots_daemon daemon
Restart=on-failure

[Install]
WantedBy=multi-user.target

```

### How to use it:

1. **Reload systemd:** `sudo systemctl daemon-reload`
2. **Start the service:** `sudo systemctl start sunspots`
3. **Check status:** `sudo systemctl status sunspots`
4. **Stop (Graceful):** `sudo systemctl stop sunspots` (This sends the `SIGTERM` that triggers your reapers!)
5. **Enable on boot:** `sudo systemctl enable sunspots`

**Would you like me to add a `SIGUSR1` signal handler to the code so you can trigger a live status report to the syslog whenever you want?**
