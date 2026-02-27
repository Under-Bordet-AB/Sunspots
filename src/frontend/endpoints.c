#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "http_parser.h"

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

char* load_file(const char* path, size_t* out_size)
{
    if (!path)
        return NULL;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return NULL;
    }

    // Must be regular file
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        return NULL;
    }

    // Reject symlinks explicitly (extra safety)
    if (S_ISLNK(st.st_mode)) {
        close(fd);
        return NULL;
    }

    // Size validation
    if (st.st_size < 0 || st.st_size == 9223372036854775807) {
        close(fd);
        return NULL;
    }

    size_t size = (size_t)st.st_size;

    char* buffer = malloc(size + 1);
    if (!buffer) {
        close(fd);
        return NULL;
    }

    size_t total_read = 0;
    while (total_read < size) {
        ssize_t r = read(fd, buffer + total_read, size - total_read);
        if (r <= 0) {
            free(buffer);
            close(fd);
            return NULL;
        }
        total_read += (size_t)r;
    }

    buffer[size] = '\0';  // safe for binary
    close(fd);

    if (out_size)
        *out_size = size;

    return buffer;
}

int sanitize_path(const char* url_path, char* out_path, size_t out_size)
{
    // Must start with /
    if (url_path[0] != '/')
        return -1;

    // Reject traversal attempts
    if (strstr(url_path, ".."))
        return -1;

    // Resolve document root
    char doc_root[PATH_MAX];
    if (!realpath(FILE_SEARCH_DIR, doc_root))
        return -1;

    // Build the final path
    char temp[PATH_MAX];
    snprintf(temp, sizeof(temp), "%s%s", doc_root, url_path);

    // Ensure we didn't overflow
    if (strlen(temp) >= PATH_MAX)
        return -1;

    strncpy(out_path, temp, out_size - 1);
    out_path[out_size - 1] = '\0';

    return 0;
}

http_response* process_request(http_request* req)
{
    if(!req || !req->path)
        return NULL;

    if(req->method == OPTIONS)
    {
        http_response* resp = http_response_init(200, "", 0);
        if(!resp)
            return NULL;
        return resp;
    }

    str_to_lower((char*)req->path);
    
    // Static endpoints

    if(strcmp(req->path, "/health") == 0)
    {
        http_response* resp = http_response_init(200, "{\"status\":\"ok\"}", HTTPRESPONSE_BODYLEN_AUTODETECT);
        if(!resp)
            return NULL;
        http_response_add_header(resp, "Content-Type", "application/json");
        return resp;
    }

    if(strcmp(req->path, "/") == 0)
    {
        http_response* resp = http_response_init(200, "Welcome to Sunspots!", HTTPRESPONSE_BODYLEN_AUTODETECT);
        if(!resp)
            return NULL;
        http_response_add_header(resp, "Content-Type", "text/plain");
        return resp;
    }

    // Treat URL as file path

    if(ALLOW_SEARCH)
    {
        char file_path[PATH_MAX];
        if(sanitize_path(req->path, file_path, sizeof(file_path)) == 0)
        {
            size_t file_size;
            char* file_data = load_file(file_path, &file_size);
            if (!file_data)
            {
                http_response* resp = http_response_init(404, "Not Found", HTTPRESPONSE_BODYLEN_AUTODETECT);
                if(!resp)
                    return NULL;
                http_response_add_header(resp, "Content-Type", "text/plain");
                return resp;
            }

            http_response* resp = http_response_init(200, file_data, file_size);
            if (!resp) {
                free(file_data);
                return NULL;
            }

            http_response_add_header(resp, "Content-Type", guess_mime_type(file_path));

            free(file_data);
            return resp;
        }
    }

    http_response* resp = http_response_init(404, "Not Found", HTTPRESPONSE_BODYLEN_AUTODETECT);
    if(!resp)
        return NULL;
    http_response_add_header(resp, "Content-Type", "text/plain");
    return resp;
}
