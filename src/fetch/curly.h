#ifndef CURLY_H
#define CURLY_H

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Opaque type - internal structure hidden in implementation
typedef struct curly_t curly_t;

// Initialize curly - allocates and initializes the client structure
// Returns 0 on success, -1 on error
int curly_init(curly_t** client);

// Make an HTTP request to the specified URL
// Returns 0 on success, -1 on error
int curly_make_request(curly_t** client, const char* url);

// Poll for request progress
// Returns 0 on success, -1 on error
int curly_poll(curly_t** client);

// Check if the request is still running
// Returns 1 if still running, 0 if complete, -1 on error
int curly_is_running(curly_t** client);

// Read the response data
// Allocates memory for buffer - caller must free it
// Returns 0 on success, -1 on error
int curly_read_response(curly_t** client, char** buffer);

// Reset client for reuse (clears response buffer)
// Returns 0 on success, -1 on error
int curly_reset(curly_t** client);

// Cleanup and free all resources
// Returns 0 on success, -1 on error
int curly_cleanup(curly_t** client);

#endif
