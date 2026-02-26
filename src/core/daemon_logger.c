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

/* Ring Buffer Selection */
#if   defined(RING_BUF_64)
    #define LOG_BUF_SIZE 65536
#elif defined(RING_BUF_32)
    #define LOG_BUF_SIZE 32768
#elif defined(RING_BUF_16)
    #define LOG_BUF_SIZE 16384
#elif defined(RING_BUF_8)
    #define LOG_BUF_SIZE 8192
#elif defined(RING_BUF_4)
    #define LOG_BUF_SIZE 4096
#elif defined(RING_BUF_2)
    #define LOG_BUF_SIZE 2048
#else
    #define LOG_BUF_SIZE 1024
#endif

#define MAX_MSG_SIZE 512
#define MAX_EVENTS   2

typedef struct journal_log
{
    char    ring_buffer[LOG_BUF_SIZE];
    char   *path_to_log;
    char   *socket_path;
    size_t  offset;
    int     sig_number;
    int     hb_interval;

	int     log_file_fd;
    int     socket_fd;
    int     timer_fd;
    int     epoll_fd;
} journal_log_t;

static int full_write(int fd, const void *buf, size_t len);
static void daemon_logger_write(journal_log_t *self);
static int daemon_logger_set_config(journal_log_t *self);
static int daemon_logger_socket_init(journal_log_t *self);
static int daemon_logger_run(journal_log_t *self);
static int daemon_logger_init(journal_log_t **self_ptr);
static int daemon_logger_deinit(journal_log_t **self_ptr);


int main(void)
{
    openlog("SUNSPOTS_LOGGER", LOG_PID, LOG_DAEMON);
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

static int daemon_logger_init(journal_log_t **self_ptr)
{
    if (!self_ptr) return -1;
	
	journal_log_t *self = calloc(1, sizeof(journal_log_t));
	/** So that deinit logic works */
	self->log_file_fd = -1;
	self->socket_fd   = -1;
	self->timer_fd    = -1;
	self->epoll_fd    = -1;
	if (!self)
	{
		syslog(LOG_ERR, "Allocation failed for daemon logger.");
		free(self);
		return -1;
	}
    if (daemon_logger_set_config(self) != 0)
	{
		syslog(LOG_ERR, "Failed to set parse module and system config for daemon logger.");
		free(self);
		return 1;
	}

    self->log_file_fd = open(self->path_to_log, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (self->log_file_fd < 0)
	{
        syslog(LOG_ERR, "Failed to create log_file_fd.");
		daemon_logger_deinit(&self);
		return -1;
	}

    if (daemon_logger_socket_init(self) != 0)
	{
        daemon_logger_deinit(&self);
        return -1;	
	}

    if ((self->timer_fd = timerfd_create(CLOCK_MONOTONIC, 0)) == -1)
	{
        daemon_logger_deinit(&self);
		return -1;
	}
	
    struct itimerspec its =
	{
        .it_interval = { .tv_sec = self->hb_interval },
        .it_value    = { .tv_sec = self->hb_interval }
    };

	if (timerfd_settime(self->timer_fd, 0, &its, NULL) == -1)
	{
        daemon_logger_deinit(&self);
		return -1;
	}

    if ((self->epoll_fd = epoll_create1(0)) == -1)
	{
        daemon_logger_deinit(&self);
		return -1;
	}
    struct epoll_event ev = { .events = EPOLLIN };
    ev.data.fd = self->socket_fd;
    epoll_ctl(self->epoll_fd, EPOLL_CTL_ADD, self->socket_fd, &ev);
    ev.data.fd = self->timer_fd;
    epoll_ctl(self->epoll_fd, EPOLL_CTL_ADD, self->timer_fd, &ev);

    *self_ptr = self;
    return 0;
}

static int daemon_logger_deinit(journal_log_t **self_ptr)
{
    if (!self_ptr || !*self_ptr)
	{
		syslog(LOG_ERR, "Self_ptr or dereferenced is NULL.");
		return -1;
	}
	
    journal_log_t *self = *self_ptr;
	if (self->path_to_log) free(self->path_to_log);
	if (self->socket_path) free(self->socket_path);
	if (self->log_file_fd >= 0) close(self->log_file_fd);
	if (self->socket_fd)
	{
		epoll_ctl(self->epoll_fd, EPOLL_CTL_DEL, self->socket_fd, NULL);
		close(self->socket_fd);
	}
	if (self->timer_fd)
	{
		epoll_ctl(self->epoll_fd, EPOLL_CTL_DEL, self->timer_fd, NULL);
		close(self->timer_fd);
	}
	if (self->epoll_fd)    close(self->epoll_fd);
	
	free(self);
	*self_ptr = NULL;
    syslog(LOG_INFO, "Closed and freed resources for logger.");
	return 0;
}


static int daemon_logger_run(journal_log_t *self)
{
    struct epoll_event events[MAX_EVENTS];
    char msg_buf[MAX_MSG_SIZE];

    while (1)
    {
        int nfds = epoll_wait(self->epoll_fd, events, MAX_EVENTS, -1);
        if (nfds < 0 && errno != EINTR)
		{
			syslog(LOG_ERR, "NFDS < 0 and ERRNO is not EINTR. ERRNO: %m");			
			return -1;
		}

        for (int i = 0; i < nfds; i++)
        {
            if (events[i].data.fd == self->socket_fd)
            {
                while (1)
                {
                    ssize_t n = recvfrom(self->socket_fd, msg_buf, sizeof(msg_buf) - 1, 0, NULL, NULL);
                    if (n < 0)
                    {
                        if (errno != EAGAIN && errno != EWOULDBLOCK)
						{
							syslog(LOG_ERR, "Failed recvfrom. ERRNO: %m");
							return -1;
						}
						break;
                    }
                    /** Will adding this new message overflow our memory buffer?
						If yes, flush the current buffer to the disk right now to make room. */
                    if (self->offset + (size_t)n + 1 > LOG_BUF_SIZE)
					{
						daemon_logger_write(self);
					}                        
                    /** Otherwise fill up buffer to avoid syscall overhead */
                    memcpy(self->ring_buffer + self->offset, msg_buf, n);
                    self->offset += n;
                    self->ring_buffer[self->offset++] = '\n';
                }
            }
            else if (events[i].data.fd == self->timer_fd)
            {
                uint64_t expirations;
                if (read(self->timer_fd, &expirations, sizeof(expirations)) > 0)
                {
                    daemon_logger_write(self);
                    if (self->sig_number > 0)
                    {
                        pid_t ppid = getppid();
                        if (ppid > 1) kill(ppid, self->sig_number);
                    }
                }
            }
        }
    }
    return 0;
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
        p += n;
        len -= n;
    }
    return 0;
}

static void daemon_logger_write(journal_log_t *self)
{
    if (!self || self->offset == 0) return;

    struct stat st;
    if (fstat(self->log_file_fd, &st) == 0)
    {    
        if ((size_t)st.st_size + self->offset > LOG_BUF_SIZE)
        {
            if (ftruncate(self->log_file_fd, 0) != 0)
            {                
                syslog(LOG_ERR, "Failed to truncate log file: %m");
				goto cleanup;
            }
        }
    }



    if(full_write(self->log_file_fd, self->ring_buffer, self->offset) != 0)
	{
		syslog(LOG_ERR, "Failed to flush buffer to disk: %m");
	}
	
cleanup:
    self->offset = 0;
}

static int daemon_logger_set_config(journal_log_t *self)
{
    char *config_env = getenv("SUNSPOTS_CONFIG");
    if (!config_env) return -1;
    cJSON *mod = cJSON_Parse(config_env);
    if (!mod) return -1;

    cJSON *path = cJSON_GetObjectItemCaseSensitive(mod, "log_path");
    cJSON *sock = cJSON_GetObjectItemCaseSensitive(mod, "socket_path");
    cJSON *hb   = cJSON_GetObjectItemCaseSensitive(mod, "heartbeat_interval");

    if (cJSON_IsString(path)) self->path_to_log = strdup(path->valuestring);
    if (cJSON_IsString(sock)) self->socket_path = strdup(sock->valuestring);
    if (cJSON_IsNumber(hb))   self->hb_interval = hb->valueint;

    cJSON_Delete(mod);

    char *sig_env = getenv("SUNSPOTS_SIGNAL");
    if (sig_env) self->sig_number = atoi(sig_env);

    return (self->path_to_log && self->socket_path && self->hb_interval > 0) ? 0 : -1;
}


static int daemon_logger_socket_init(journal_log_t *self)
{
    self->socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (self->socket_fd < 0) return -1;

    int flags = fcntl(self->socket_fd, F_GETFL, 0);
    fcntl(self->socket_fd, F_SETFL, flags | O_NONBLOCK);

    unlink(self->socket_path);

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, self->socket_path, sizeof(addr.sun_path) - 1);

    return bind(self->socket_fd, (struct sockaddr *)&addr, sizeof(addr));
}
