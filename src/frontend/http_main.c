#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "frontend/client_queue.h"
#include "frontend/http_constants.h"
#include "frontend/http_main.h"
#include "frontend/http_worker.h"

http_server* http_init()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return NULL;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_UNSPEC;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(HTTP_PORT);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return NULL;
    }

    if (listen(server_fd, 16) < 0) {
        perror("listen");
        close(server_fd);
        return NULL;
    }

    http_server* server = calloc(1, sizeof(http_server));
    if(server == NULL)
    {
        printf("Out of memory allocating http_server.\n");
        return NULL;
    }

    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    server->server_fd = server_fd;

    printf("Created TCP host on port %d\n", HTTP_PORT);

    for (int i = 0; i < LISTENER_COUNT; i++) {
        pthread_create(&server->workers[i], NULL, http_worker_thread, NULL);
    }

    return server;
}

int http_accept(http_server* server)
{
    int accepts = 0;
    while (1) {
        int client_fd = accept(server->server_fd, NULL, NULL);
        if (client_fd >= 0) {
            enqueue_client(client_fd);
            accepts++;
        } else {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                perror("accept");
            }
        }
    }
    return accepts;
}

void http_dispose(http_server** server_var)
{
    http_server* server = *server_var;
    close(server->server_fd);
    
    int i;
    for(i = 0; i < LISTENER_COUNT; i++)
    {
        enqueue_client(SIGNAL_EXIT);
    }
    for(i = 0; i < LISTENER_COUNT; i++)
    {
        pthread_join(server->workers[i], NULL);
    }

    free(server);
    *server_var = NULL;
}