#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "endpoints.h"
#include "http_parser.h"
#include "client_queue.h"
#include "http_constants.h"
#include "linked_list.h"

void* http_worker_thread(void* arg) {
    (void)arg;

    printf("[Thread-%ld] Ready for connections!\n", pthread_self());

    while (1) {
        int client_fd = dequeue_client();
        if(client_fd == SIGNAL_EXIT)
        {
            printf("[Thread-%ld] Exit signal received.\n", pthread_self());
            break;
        }
        printf("[Thread-%ld] Client connected (fd=%d)\n",
               pthread_self(), client_fd);

        char buffer[2048];
        ssize_t bytes;

        int closeReason = 1;

        size_t total = 0;
        size_t skip_remaining = 0;
        while ((bytes = recv(client_fd, buffer + total, sizeof(buffer) - total - 1, 0)) > 0) {
            total += bytes;
            buffer[total] = '\0';

            // Skip body
            if (skip_remaining > 0) {
                size_t to_discard = bytes;
                if (to_discard > skip_remaining)
                    to_discard = skip_remaining;

                skip_remaining -= to_discard;

                // Shift leftover bytes
                size_t remaining = total - to_discard;
                memmove(buffer, buffer + to_discard, remaining);
                total = remaining;
                buffer[total] = '\0';

                if (skip_remaining > 0)
                    continue;
            }

            char* location = strstr(buffer, "\r\n\r\n");
            if (location && skip_remaining == 0) {
                http_request* req = http_parse_request(buffer);
                if (!req)
                {
                    printf("[Thread-%ld] Request could not be parsed, likely invalid.\n", pthread_self());
                    break;
                }

                size_t header_end_offset = (location - buffer) + 4;

                const char* len = http_get_header(req, "Content-Length");
                if (len) {
                    // We received a body that we don't care about, skip it
                    long content_length = strtoul(len, NULL, 10);
                    skip_remaining = (size_t)content_length;
                } else {
                    skip_remaining = 0;
                }

                // Consume any body bytes already received
                size_t body_bytes_read = total - header_end_offset;
                if (body_bytes_read >= skip_remaining) {
                    body_bytes_read = skip_remaining;
                }
                skip_remaining -= body_bytes_read;

                // Shift remaining bytes down
                size_t remaining = total - (header_end_offset + body_bytes_read);
                memmove(buffer, buffer + header_end_offset + body_bytes_read, remaining);

                total = remaining;
                buffer[total] = '\0';

                // Handle request
                printf("[Thread-%ld] %s received: %s\n", pthread_self(), RequestMethod_tostring(req->method), req->path);
                
                http_response* resp = process_request(req);
                if(!resp)
                {
                    printf("[Thread-%ld] Failed to generate a response, disconnecting client.\n", pthread_self());
                    break;
                }

                if(strlen(CORS_ALLOWED_ORIGIN) > 0)
                    http_response_add_header(resp, "Access-Control-Allow-Origin", CORS_ALLOWED_ORIGIN);
                if(strlen(CORS_ALLOWED_METHODS) > 0)
                    http_response_add_header(resp, "Access-Control-Allow-Methods", CORS_ALLOWED_METHODS);
                if(strlen(CORS_ALLOWED_HEADERS) > 0)
                    http_response_add_header(resp, "Access-Control-Allow-Headers", CORS_ALLOWED_HEADERS);

                int shouldBreak = 0;
                const char* hdr = http_get_header(req, "Connection");
                if(hdr && strcmp(hdr, "keep-alive") != 0)
                {
                    shouldBreak = 1;
                    closeReason = 2;
                } else {
                    http_response_add_header(resp, "Connection", "keep-alive");
                }

                size_t response_size = 0;
                const char* response_str = http_response_stringify(resp, &response_size);
                
                send(client_fd, response_str, response_size, 0);

                free((char*)response_str);

                http_request_dispose(&req);
                http_response_dispose(&resp);

                if(shouldBreak)
                    break;

                continue;
            }
        }

        if(closeReason == 2)
        {
            printf("[Thread-%ld] Ending client connection due to keep-alive not requested. (fd=%d)\n", pthread_self(), client_fd);
        } else {
            printf("[Thread-%ld] Ending client connection due to EOF. (fd=%d)\n", pthread_self(), client_fd);
        }

        close(client_fd);
    }

    return NULL;
}