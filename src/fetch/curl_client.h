#ifndef CURL_CLIENT_H
#define CURL_CLIENT_H

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct memory_struct {
    char* memory;
    size_t size;
};

typedef struct {
    CURLM* multi_handle;
    CURL* easy_handle;
    int still_running;
    struct memory_struct mem;
} curl_client_t;

// Initialize curl_client - allocates and initializes the client structure
// Returns 0 on success, -1 on error
int curl_client_init(curl_client_t** client);

// Make an HTTP request to the specified URL
// Returns 0 on success, -1 on error
int curl_client_make_request(curl_client_t** client, const char* url);

// Poll for request progress
// Returns 0 on success, -1 on error
int curl_client_poll(curl_client_t** client);

// Check if the request is still running
// Returns 1 if still running, 0 if complete, -1 on error
int curl_client_is_running(curl_client_t** client);

// Read the response data
// Allocates memory for buffer - caller must free it
// Returns 0 on success, -1 on error
int curl_client_read_response(curl_client_t** client, char** buffer);

// Reset client for reuse (clears response buffer)
// Returns 0 on success, -1 on error
int curl_client_reset(curl_client_t** client);

// Cleanup and free all resources
// Returns 0 on success, -1 on error
int curl_client_cleanup(curl_client_t** client);

#endif