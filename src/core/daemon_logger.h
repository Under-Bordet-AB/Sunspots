/**
 * @file sunspots_log.h
 * @brief Fire-and-forget log sender over a Unix domain socket.
 *        Include this header in any module that needs to log.
 *        The socket path must match the "socket_path" in the config.
 */

#ifndef DAEMON_LOGGER_H
#define DAEMON_LOGGER_H

#include "../libs/json/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

static inline void daemon_logger_send(const char *tag, const char *msg)
{
    if (!msg) return;

    char *system_env = getenv("SUNSPOTS_SYSTEM");
    if (!system_env) return;

    cJSON *sys = cJSON_Parse(system_env);
    if (!sys) return;

    cJSON *sock = cJSON_GetObjectItemCaseSensitive(sys, "socket_path");
    if (!cJSON_IsString(sock)) { cJSON_Delete(sys); return; }

    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) { cJSON_Delete(sys); return; }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock->valuestring, sizeof(addr.sun_path) - 1);

    char buf[512];
    int  len = snprintf(buf, sizeof(buf), "[%s] %s", tag ? tag : "?", msg);
    if (len > 0)
    {
        sendto(fd, buf, (size_t)len, 0, (struct sockaddr *)&addr, sizeof(addr));
    }

    close(fd);
    cJSON_Delete(sys);
}
#endif /* SUNSPOTS_LOG_H */
