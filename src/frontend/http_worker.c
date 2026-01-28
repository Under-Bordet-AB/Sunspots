#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "frontend/client_queue.h"
#include "frontend/http_constants.h"
#include "frontend/http_parser.h"
#include "libs/linked_list/linked_list.h"

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
                printf("[Thread-%ld] Request received: %s\n", pthread_self(), req->path);
                if(strcmp(req->path, "/health") == 0)
                {
                    char testResponse[256];
                    snprintf(testResponse, 256, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: keep-alive\r\nContent-Length: 33\r\n\r\nHello from Thread-%li", pthread_self());
                    send(client_fd, testResponse, strlen(testResponse), 0);
                } else {
                    char testResponse[256];
                    snprintf(testResponse, 256, "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nConnection: keep-alive\r\nContent-Length: 33\r\n\r\nHello from Thread-%li", pthread_self());
                    send(client_fd, testResponse, strlen(testResponse), 0);
                }

                http_request_dispose(&req);

                continue;
            }
        }

        printf("[Thread-%ld] Ending client connection. (fd=%d)\n",
               pthread_self(), client_fd);

        close(client_fd);
    }

    return NULL;
}