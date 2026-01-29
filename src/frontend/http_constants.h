#ifndef HTTP_CONSTANTS_H
#define HTTP_CONSTANTS_H

#define HTTP_PORT 10480  // Port to host HTTP server on
#define HTTPS_PORT 10480 // Port to host HTTPS (TLS) server on | CURRENTLY UNUSED
#define LISTENER_COUNT 4 // How many pthreads to start for HTTP connection handling

#define LISTEN_QUEUE 16 // Parameter for listen(), how many connections to queue before refusing
#define QUEUE_SIZE 32 // Size of client fd queue (circular buffer), set to a higher value for overflow protection
#define SIGNAL_EXIT -1

#define HTTP_VERSION "HTTP/1.1" // We should support keep-alive
#define MAX_URL_LEN 256
#define CORS_ALLOWED_ORIGIN "*"
#define CORS_ALLOWED_METHODS "GET, OPTIONS"
#define CORS_ALLOWED_HEADERS "Content-Type"

#endif