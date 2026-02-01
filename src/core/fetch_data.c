
/**
 * fetch_data.c - Worker process with Inotify + TimerFD + Epoll
 **/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/inotify.h>
#include <sys/syslog.h>
#include <time.h>

/* --- 1. DEFINITIONS & DISPATCH TABLE SETUP --- */

typedef enum
{
    SOURCE_TIMER = 0,
    SOURCE_INOTIFY,
    SOURCE_COUNT
} event_source_t;

// Context to pass data (like PPID) to generic handlers
typedef struct {
    pid_t ppid;
} app_context_t;

typedef void (*event_handler)(int fd, void *ctx);

typedef struct dispatch_entry
{
    int fd;
    event_handler handler;
    const char *name;
} dispatch_entry_t;

typedef struct system_state
{
    int epoll_fd;
    dispatch_entry_t sources[SOURCE_COUNT];
} system_state_t;

// Forward declarations
int handle_signal(pid_t pid);
void handle_timer(int fd, void *ctx);
void handle_inotify(int fd, void *ctx);

/* --- 2. MAIN EXECUTION --- */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        return EXIT_FAILURE;
    }

    // 1. Initialize Logging and Context
    openlog("SUNSPOTS_FETCH", LOG_PID, LOG_CONS);
    syslog(LOG_NOTICE, "Fetch data worker started.");
    
    app_context_t ctx;
    ctx.ppid = (pid_t)atoi(argv[1]);

    system_state_t sys = {0};

    // 2. SETUP INOTIFY (File Watcher)
    const char *file_to_watch = "/home/drone/Code/c/system_c_cpp/boiler_room_prj/Sunspots/src/core/output.envar";
	uint32_t mask = IN_CLOSE_WRITE;
    
    int inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd < 0) {
        syslog(LOG_ERR, "inotify_init1 failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    int wd = inotify_add_watch(inotify_fd, file_to_watch, mask);
    if (wd < 0) {
        // Fallback: If file doesn't exist yet, we might want to wait or exit. 
        // For now, we log and exit to alert the Daemon.
        syslog(LOG_ERR, "Failed to watch file %s: %s", file_to_watch, strerror(errno));
        exit(EXIT_FAILURE);
    }
    syslog(LOG_INFO, "Inotify watching: %s", file_to_watch);

    // 3. SETUP TIMERFD (Heartbeat Metronome)
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer_fd < 0) {
        syslog(LOG_ERR, "timerfd_create failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct itimerspec ts;
    ts.it_interval.tv_sec = 1;  // Repeat every 1 second
    ts.it_interval.tv_nsec = 0;
    ts.it_value.tv_sec = 1;     // First expiration after 1 second
    ts.it_value.tv_nsec = 0;
    
    if (timerfd_settime(timer_fd, 0, &ts, NULL) < 0) {
        syslog(LOG_ERR, "timerfd_settime failed");
        exit(EXIT_FAILURE);
    }

    // 4. POPULATE DISPATCH TABLE
    sys.sources[SOURCE_TIMER].fd = timer_fd;
    sys.sources[SOURCE_TIMER].handler = handle_timer;
    sys.sources[SOURCE_TIMER].name = "Heartbeat Timer";

    sys.sources[SOURCE_INOTIFY].fd = inotify_fd;
    sys.sources[SOURCE_INOTIFY].handler = handle_inotify;
    sys.sources[SOURCE_INOTIFY].name = "File Watcher";

    // 5. SETUP EPOLL & REGISTER DISPATCH KEYS
    // Note: epoll_create1(0) is preferred over epoll_create(0)
    sys.epoll_fd = epoll_create1(0); 
    if (sys.epoll_fd < 0) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev;
    
    // Register Timer: Use the ENUM Index as the key!
    ev.events = EPOLLIN;
    ev.data.u32 = SOURCE_TIMER; 
    if (epoll_ctl(sys.epoll_fd, EPOLL_CTL_ADD, timer_fd, &ev) == -1) {
        perror("epoll_ctl: timer");
        exit(EXIT_FAILURE);
    }

    // Register Inotify: Use the ENUM Index as the key!
    ev.events = EPOLLIN;
    ev.data.u32 = SOURCE_INOTIFY;
    if (epoll_ctl(sys.epoll_fd, EPOLL_CTL_ADD, inotify_fd, &ev) == -1) {
        perror("epoll_ctl: inotify");
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Event loop starting. Entering the Matrix.");

    /* --- 3. THE EVENT LOOP --- */
    struct epoll_event events[10];

    while (1)
    {
        // Wait indefinitely (-1). The timer guarantees we wake up every second.
        int nfds = epoll_wait(sys.epoll_fd, events, 10, -1);

        if (nfds == -1) {
            if (errno == EINTR) continue; // Interrupted by signal
            syslog(LOG_ERR, "epoll_wait failed");
            break;
        }

        for (int i = 0; i < nfds; ++i)
        {
            // 1. Extract the Index (The Key)
            uint32_t idx = events[i].data.u32;

            // 2. Safety Check
            if (idx >= SOURCE_COUNT) continue;

            // 3. Dispatch the Handler
            // We pass &ctx so the handlers have access to the PPID
            dispatch_entry_t *entry = &sys.sources[idx];
            entry->handler(entry->fd, &ctx);
        }
    }

    close(inotify_fd);
    close(timer_fd);
    close(sys.epoll_fd);
    closelog();
    return EXIT_SUCCESS;
}

/* --- 4. HANDLER IMPLEMENTATIONS --- */

// The actual signal sender
int handle_signal(pid_t pid)
{
    // Use SIGRTMIN for reliable queuing
    if (kill(pid, SIGRTMIN) == -1)
    {
        syslog(LOG_ERR, "Failed to send heartbeat to PID %d: %s", pid, strerror(errno));
        // If parent is dead, we should probably die too
        exit(EXIT_FAILURE);
    }
    return 0;
}

// The Timer Handler: Wakes up, Reads Timer, Calls Signal
void handle_timer(int fd, void *ctx)
{
    app_context_t *app_ctx = (app_context_t *)ctx;
    uint64_t exp;
    
    // We MUST read the timerfd to clear the event, otherwise epoll spins at 100% CPU
    ssize_t s = read(fd, &exp, sizeof(exp));
    if (s != sizeof(exp)) {
        syslog(LOG_ERR, "Timer read error");
    }

    // "Fall through" logic: Timer fired, so we send the heartbeat
    // syslog(LOG_DEBUG, "Timer tick. Pulsing heartbeat.");
    handle_signal(app_ctx->ppid);
}

// The File Watcher Handler
void handle_inotify(int fd, void *ctx)
{
    char buff[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    ssize_t len;
    
    // Read all available events
    while (1)
    {
        len = read(fd, buff, sizeof(buff));
        
        if (len == -1) {
            if (errno == EAGAIN) break; // No more data
            syslog(LOG_ERR, "Inotify read error");
            exit(EXIT_FAILURE);
        }
        
        if (len <= 0) break;

        const struct inotify_event *event;
        for (char *ptr = buff; ptr < buff + len; ptr += sizeof(struct inotify_event) + event->len)
        {
            event = (const struct inotify_event *) ptr;
            
            if (event->mask & IN_CLOSE_WRITE)
            {
                syslog(LOG_NOTICE, "Target file modified! Triggering logic.");
                // Add your file parsing logic here
            }
        }
    }
}

