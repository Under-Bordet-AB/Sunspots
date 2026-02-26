#include "http_constants.h"

int HTTP_PORT = 10480;  // (server_port_http)    Port to host HTTP server on
int HTTPS_PORT = 10480; // (server_port_https)   Port to host HTTP server on
int LISTENER_COUNT = 8; // (server_threads)      How many pthreads to start for HTTP connection handling

int LISTEN_QUEUE = 16;  // (server_listen_queue) Parameter for listen(), how many connections to queue before refusing
int QUEUE_SIZE = 32;    // (client_queue_size)   Size of client fd queue (circular buffer), set to a higher value for overflow protection

int ALLOW_SEARCH = 1;   // (allow_file_search)   Enables endpoint fallback that treats the request URL as a file path to try and return
char* FILE_SEARCH_DIR;  // (file_search_dir)     The base path to search through following request URLs, the fallback value is defined in frontend_main.c for safety