#include "../libs/json/cJSON.h"
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

/* Log file rotation ceiling
 * Controlled by the same BUF_ flags in CMakeLists (currently BUF_8 = 8 KB).
 * When a new message would push the file past this ceiling it is truncated
 * to zero before writing — the frontend always reads the whole file so this
 * acts as a clean scroll rather than a roll.
 */

#if   defined(BUF_64)
    #define LOG_FILE_MAX 65536
#elif defined(BUF_32)
    #define LOG_FILE_MAX 32768
#elif defined(BUF_16)
    #define LOG_FILE_MAX 16384
#elif defined(BUF_8)
    #define LOG_FILE_MAX 8192
#elif defined(BUF_4)
    #define LOG_FILE_MAX 4096
#elif defined(BUF_2)
    #define LOG_FILE_MAX 2048
#else
    #define LOG_FILE_MAX 1024
#endif

#define MAX_MSG_SIZE 512
#define MAX_EVENTS   2

static volatile sig_atomic_t g_running = 1;

typedef struct journal_log
{
    char   *path_to_log;
    char   *socket_path;
    int     sig_number;
    int     hb_interval;

    int     log_file_fd;
    int     socket_fd;
    int     timer_fd;
    int     epoll_fd;
} journal_log_t;

static void signal_handler(int signum);
static int  full_write(int fd, const void *buf, size_t len);
static void daemon_logger_write(journal_log_t *self, const char *msg, size_t len);
static int  daemon_logger_set_config(journal_log_t *self);
static int  daemon_logger_socket_init(journal_log_t *self);
static int  daemon_logger_run(journal_log_t *self);
static int  daemon_logger_init(journal_log_t **self_ptr);
static int  daemon_logger_deinit(journal_log_t **self_ptr);

int main(void)
{
    openlog("SUNSPOTS_LOGGER", LOG_PID, LOG_DAEMON);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

    journal_log_t *log = NULL;
    if (daemon_logger_init(&log) != 0)
    {
        syslog(LOG_ERR, "Failed to init daemon logger");
        return EXIT_FAILURE;
    }

    if (daemon_logger_run(log) != 0)
    {
        syslog(LOG_ERR, "Failed to run daemon logger.");
        daemon_logger_deinit(&log);
        return EXIT_FAILURE;
    }

    daemon_logger_deinit(&log);
    return EXIT_SUCCESS;
}

static void signal_handler(int signum)
{
    (void)signum;
    g_running = 0;
}

static int daemon_logger_set_config(journal_log_t *self)
{
    char *sys_env = getenv("SUNSPOTS_SYSTEM");
    char *mod_env = getenv("SUNSPOTS_CONFIG");

    if (!sys_env || !mod_env) return -1;

    cJSON *root = cJSON_Parse(sys_env);
    cJSON *mod  = cJSON_Parse(mod_env);
    if (!root || !mod)
    {
        cJSON_Delete(root);
        cJSON_Delete(mod);
        return -1;
    }

    cJSON *sock = cJSON_GetObjectItemCaseSensitive(root, "socket_path");
    if (cJSON_IsString(sock))
    {
        self->socket_path = strdup(sock->valuestring);
    }

    cJSON *path = cJSON_GetObjectItemCaseSensitive(mod, "log_path");
    cJSON *hb   = cJSON_GetObjectItemCaseSensitive(mod, "heartbeat_interval");

    if (cJSON_IsString(path)) self->path_to_log = strdup(path->valuestring);
    if (cJSON_IsNumber(hb))   self->hb_interval  = hb->valueint;

    cJSON_Delete(root);
    cJSON_Delete(mod);

    char *sig_env = getenv("SUNSPOTS_SIGNAL");
    if (sig_env) self->sig_number = atoi(sig_env);

    return (self->path_to_log && self->socket_path && self->hb_interval > 0) ? 0 : -1;
}

static int daemon_logger_socket_init(journal_log_t *self)
{
    self->socket_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (self->socket_fd < 0) return -1;

    unlink(self->socket_path);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, self->socket_path, sizeof(addr.sun_path) - 1);

    if (bind(self->socket_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) return -1;

    chmod(self->socket_path, 0666);
    return 0;
}

static void daemon_logger_write(journal_log_t *self, const char *msg, size_t len)
{
    if (!self || !msg || len == 0) return;

    struct stat st;
    if (fstat(self->log_file_fd, &st) == 0
        && (size_t)st.st_size + len + 1 > LOG_FILE_MAX)
    {
        if (ftruncate(self->log_file_fd, 0) < 0)
        {
            syslog(LOG_WARNING, "Failed to truncate log file: %m");
        }
    }

    if (full_write(self->log_file_fd, msg, len) < 0)
    {
        syslog(LOG_WARNING, "Failed to write message to log file: %m");
        return;
    }
    full_write(self->log_file_fd, "\n", 1);
}

static int full_write(int fd, const void *buf, size_t len)
{
    const char *p = buf;
    while (len > 0)
    {
        ssize_t n = write(fd, p, len);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            return -1;
        }
        p   += n;
        len -= (size_t)n;
    }
    return 0;
}

static int daemon_logger_init(journal_log_t **self_ptr)
{
    if (!self_ptr) return -1;

    journal_log_t *self = calloc(1, sizeof(journal_log_t));
    if (!self) return -1;

    self->log_file_fd = -1;
    self->socket_fd   = -1;
    self->timer_fd    = -1;
    self->epoll_fd    = -1;

    if (daemon_logger_set_config(self) != 0)
    {
        syslog(LOG_ERR, "Failed to parse config for daemon logger.");
        free(self);
        return -1;
    }

    self->log_file_fd = open(self->path_to_log, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (self->log_file_fd < 0)
    {
        syslog(LOG_ERR, "Failed to open log file %s: %m", self->path_to_log);
        daemon_logger_deinit(&self);
        return -1;
    }

    if (daemon_logger_socket_init(self) != 0)
    {
        syslog(LOG_ERR, "Failed to init socket: %m");
        daemon_logger_deinit(&self);
        return -1;
    }

    self->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (self->timer_fd < 0)
    {
        syslog(LOG_ERR, "Failed to create timerfd: %m");
        daemon_logger_deinit(&self);
        return -1;
    }

    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    its.it_interval.tv_sec = self->hb_interval;
    its.it_value.tv_sec    = self->hb_interval;

    if (timerfd_settime(self->timer_fd, 0, &its, NULL) < 0)
    {
        syslog(LOG_ERR, "Failed to arm timerfd: %m");
        daemon_logger_deinit(&self);
        return -1;
    }

    self->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (self->epoll_fd < 0)
    {
        syslog(LOG_ERR, "Failed to create epoll fd: %m");
        daemon_logger_deinit(&self);
        return -1;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events  = EPOLLIN;
    ev.data.fd = self->socket_fd;

    if (epoll_ctl(self->epoll_fd, EPOLL_CTL_ADD, self->socket_fd, &ev) < 0)
    {
        syslog(LOG_ERR, "Failed to add socket_fd to epoll: %m");
        daemon_logger_deinit(&self);
        return -1;
    }

    ev.data.fd = self->timer_fd;
    if (epoll_ctl(self->epoll_fd, EPOLL_CTL_ADD, self->timer_fd, &ev) < 0)
    {
        syslog(LOG_ERR, "Failed to add timer_fd to epoll: %m");
        daemon_logger_deinit(&self);
        return -1;
    }

    *self_ptr = self;
    return 0;
}

static int daemon_logger_run(journal_log_t *self)
{
    struct epoll_event events[MAX_EVENTS];
    char               msg_buf[MAX_MSG_SIZE];

    while (g_running)
    {
        int nfds = epoll_wait(self->epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0)
        {
            if (errno == EINTR) continue;
            return -1;
        }

        for (int i = 0; i < nfds; i++)
        {
            if (events[i].data.fd == self->socket_fd)
            {
                for (;;)
                {
                    ssize_t n = recvfrom(self->socket_fd, msg_buf,
                                         sizeof(msg_buf) - 1, 0, NULL, NULL);
                    if (n <= 0) break;

                    msg_buf[n] = '\0';
                    daemon_logger_write(self, msg_buf, (size_t)n);
                }
            }
            else if (events[i].data.fd == self->timer_fd)
            {
                uint64_t expirations;
                ssize_t  r = read(self->timer_fd, &expirations, sizeof(expirations));
                if (r == (ssize_t)sizeof(expirations) && self->sig_number > 0)
                {
                    kill(getppid(), self->sig_number);
                }
            }
        }
    }

    return 0;
}

static int daemon_logger_deinit(journal_log_t **self_ptr)
{
    if (!self_ptr || !*self_ptr) return -1;

    journal_log_t *self = *self_ptr;

    free(self->path_to_log);

    if (self->socket_path)
    {
        unlink(self->socket_path);
        free(self->socket_path);
    }

    if (self->log_file_fd >= 0) close(self->log_file_fd);
    if (self->socket_fd   >= 0) close(self->socket_fd);
    if (self->timer_fd    >= 0) close(self->timer_fd);
    if (self->epoll_fd    >= 0) close(self->epoll_fd);

    free(self);
    *self_ptr = NULL;
    return 0;
}
