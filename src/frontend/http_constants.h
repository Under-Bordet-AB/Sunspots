#ifndef HTTP_CONSTANTS_H
#define HTTP_CONSTANTS_H

#include "linked_list.h"

// Defines are static configs, externs are configurable by sunspots.json

extern int HTTP_PORT;
extern int HTTPS_PORT;
extern int LISTENER_COUNT;

extern int LISTEN_QUEUE;
extern int QUEUE_SIZE;
#define SIGNAL_EXIT (-1)

#define HTTP_VERSION "HTTP/1.1" // We should support keep-alive
#define MAX_URL_LEN 256
#define CORS_ALLOWED_ORIGIN "*"
#define CORS_ALLOWED_METHODS "GET, OPTIONS"
#define CORS_ALLOWED_HEADERS "Content-Type"

extern int ALLOW_SEARCH;
extern char* FILE_SEARCH_DIR;

extern LinkedList* URL_ALIASES;

#endif