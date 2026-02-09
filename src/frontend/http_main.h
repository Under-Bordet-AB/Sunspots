#ifndef HTTP_MAIN_H
#define HTTP_MAIN_H

#include <pthread.h>

#include "frontend/http_constants.h"

typedef struct
{
    int server_fd;
    pthread_t workers[LISTENER_COUNT];
} http_server;

/*
Starts the HTTP frontend and returns a pointer to a struct representing it.
Returns NULL if unsuccessful.
*/
http_server* http_init();

/*
Accepts waiting client connections, returns number of clients accepted.
*/
int http_accept(http_server* server);

/*
Close the HTTP frontend and dispose all resources.
*/
void http_dispose(http_server** server);

#endif