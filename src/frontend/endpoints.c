#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frontend/http_parser.h"

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
    
    if(strcmp(req->path, "/health") == 0)
    {
        http_response* resp = http_response_init(200, "{\"status\":\"ok\"}", -1);
        if(!resp)
            return NULL;
        http_response_add_header(resp, "Content-Type", "application/json");
        return resp;
    }

    if(strcmp(req->path, "/dog.mp4") == 0)
    {
        size_t png_size;
        char* png_data = load_file("dog.mp4", &png_size);
        if (!png_data)
        {
            http_response* resp = http_response_init(500, "Failed to load video.", -1);
            if(!resp)
                return NULL;
            http_response_add_header(resp, "Content-Type", "text/plain");
            return resp;
        }

        http_response* resp = http_response_init(200, png_data, png_size);
        if (!resp) {
            free(png_data);
            return NULL;
        }

        http_response_add_header(resp, "Content-Type", "video/mp4");

        free(png_data);
        return resp;
    }

    http_response* resp = http_response_init(404, "Not Found", -1);
    if(!resp)
        return NULL;
    http_response_add_header(resp, "Content-Type", "text/plain");
    return resp;
}