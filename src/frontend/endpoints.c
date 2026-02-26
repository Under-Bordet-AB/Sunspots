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
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }

    rewind(f);

    char* buffer = (char*)malloc(size + 1); // +1 for optional '\0'
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buffer, 1, size, f);
    fclose(f);

    if (read != (size_t)size) {
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0'; // safe even for binary
    if (out_size)
        *out_size = (size_t)size;

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