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
        char* post_buffer = NULL;

        ssize_t bytes;
        int closeReason = 1;
        size_t total = 0;
        size_t body_remaining = 0;

        while ((bytes = recv(client_fd, buffer + total, sizeof(buffer) - total - 1, 0)) > 0) {
            total += bytes;
            buffer[total] = '\0';

            // Skip body
            if (body_remaining > 0) {
                size_t to_discard = bytes;
                if (to_discard > body_remaining)
                    to_discard = body_remaining;

                body_remaining -= to_discard;

                // Shift leftover bytes
                size_t remaining = total - to_discard;
                memmove(buffer, buffer + to_discard, remaining);
                total = remaining;
                buffer[total] = '\0';

                if (body_remaining > 0)
                    continue;
            }

            char* location = strstr(buffer, "\r\n\r\n");
            if (location && body_remaining == 0) {
                http_request* req = http_parse_request(buffer);
                if (!req)
                {
                    printf("[Thread-%ld] Request could not be parsed, likely invalid.\n", pthread_self());
                    break;
                }

                size_t header_end_offset = (location - buffer) + 4;

                http_response* resp = NULL;
                const char* len = http_get_header(req, "Content-Length");
                if (len) {
                    size_t content_length = strtoul(len, NULL, 10);
                    if(req->method == POST) {
                        if(content_length > (size_t)POST_SIZE_LIMIT) {
                            resp = http_response_init(413, "", 0); // Content Too Large
                        } else {
                            post_buffer = malloc(content_length);
                            if (!post_buffer) {
                                printf("[Thread-%ld] Failed to allocate POST buffer, disconnecting client.\n", pthread_self());
                                http_request_dispose(&req);
                                break;
                            }

                            // Copy body bytes already received alongside the header
                            size_t already_received = total - header_end_offset;
                            if (already_received > content_length)
                                already_received = content_length;

                            memcpy(post_buffer, buffer + header_end_offset, already_received);
                            size_t post_total = already_received;

                            // Read any remaining body bytes directly into post_buffer
                            while (post_total < content_length) {
                                ssize_t n = recv(client_fd, post_buffer + post_total, content_length - post_total, 0);
                                if (n <= 0) break;
                                post_total += n;
                            }

                            if (post_total < content_length) {
                                printf("[Thread-%ld] Client disconnected mid-POST body, dropping request.\n", pthread_self());
                                free(post_buffer);
                                post_buffer = NULL;
                                http_request_dispose(&req);
                                break;
                            }

                            req->body = post_buffer;
                            req->body_len = post_total;

                            // Shift buffer past the consumed header+body prefix
                            size_t remaining = total - (header_end_offset + already_received);
                            memmove(buffer, buffer + header_end_offset + already_received, remaining);
                            total = remaining;
                            buffer[total] = '\0';

                            body_remaining = 0; // Already fully consumed above
                        }
                    } else {
                        // We received a body that we don't care about, skip it later
                        body_remaining = content_length;
                    }
                } else {
                    body_remaining = 0;
                }

                if(req->method != POST) {
                    size_t body_bytes_read = total - header_end_offset;
                    if (body_bytes_read >= body_remaining) {
                        body_bytes_read = body_remaining;
                    }
                    body_remaining -= body_bytes_read;

                    // Shift remaining bytes down
                    size_t remaining = total - (header_end_offset + body_bytes_read);
                    memmove(buffer, buffer + header_end_offset + body_bytes_read, remaining);
                    total = remaining;
                    buffer[total] = '\0';
                }

                // Handle request
                printf("[Thread-%ld] %s received: %s\n", pthread_self(), RequestMethod_tostring(req->method), req->path);
                
                if(!resp) // To not override 413 Content Too Large
                    resp = process_request(req);

                if(!resp)
                {
                    printf("[Thread-%ld] Failed to generate a response, disconnecting client.\n", pthread_self());
                    http_request_dispose(&req);
                    if(post_buffer) {
                        free(post_buffer);
                        post_buffer = NULL;
                    }
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

                if(post_buffer) {
                    free(post_buffer);
                    post_buffer = NULL;
                }

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

        if(post_buffer) {
            free(post_buffer);
            post_buffer = NULL;
        }
        close(client_fd);
    }

    return NULL;
}