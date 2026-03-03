/**
 * @file sunspots_log.h
 * @brief Fire-and-forget log sender over a Unix domain socket.
 *        Include this header in any module that needs to log.
 *        The socket path must match the "socket_path" in the config.
 */

#ifndef DAEMON_LOGGER_H
#define DAEMON_LOGGER_H

#include "../libs/json/cJSON.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>

static inline void daemon_logger_send(const char *sending_module, const char *msg)
{
	if (!msg) return;

	static char socket_path[108] = {0};
	static int has_path = 0;
    int fd;
	char buf[512];
	int len;
	
    /** Caching path for lifetime of program */	
	if (!has_path)
	{
		char *sys_env = getenv("SUNSPOTS_SYSTEM");
		if (!sys_env) return;
		cJSON *sys_conf = cJSON_Parse(sys_env);
		if (!sys_conf) return;
		cJSON *sp = cJSON_GetObjectItemCaseSensitive(sys_conf, "socket_path");
		if (cJSON_IsString(sp))
		{
            strncpy(socket_path, sp->valuestring, sizeof(socket_path) - 1);
			has_path = 1;
		}
		cJSON_Delete(sys_conf);		
	}

	if (!has_path) return;
	
	/** Setup unix socket */
	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd == -1) return;
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
	
    /** Get the current time for timestamp */
	struct timeval tv;
	gettimeofday(&tv, NULL);
	struct tm *now = localtime(&tv.tv_sec);
	
    /** Send message to file via socket */
	len = snprintf(buf, sizeof(buf), "%02d:%02d:%02d | %s »» %s", now->tm_hour,
				   now->tm_min,
				   now->tm_sec,
				   sending_module,
				   msg);

    if (len > 0)
	{
		sendto(fd, buf, (size_t)len, 0, (struct sockaddr*)&addr, sizeof(addr));
	}

	close(fd);
}

#endif /* SUNSPOTS_LOG_H */
