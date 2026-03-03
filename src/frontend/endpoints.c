#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "http_parser.h"
#include "file_helpers.h"

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

// If a folder is requested when auto-serving files, search for "index.*" with the following file types and serve the first one found.
static const char *index_fallbacks[] = {
    "html",
    "htm",
    "txt",
    "xml",
    "json",
    "css",
    "js"
};
#define FALLBACK_COUNT 7

int resolve_index_file(const char *dir_path, char *out_path, size_t out_size)
{
    struct stat st;

    for (int i = 0; i < FALLBACK_COUNT; i++) {
        char temp[PATH_MAX];

        snprintf(temp, sizeof(temp), "%s/index.%s",
                 dir_path, index_fallbacks[i]);

        if (stat(temp, &st) == 0 && S_ISREG(st.st_mode)) {
            if (strlen(temp) >= out_size)
                return -1;

            strcpy(out_path, temp);
            return 0;
        }
    }

    return -1;
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

    // Match URL against aliases

    if(URL_ALIASES != NULL)
    {
        LinkedList_foreach(URL_ALIASES, node)
        {
            url_alias* alias = (url_alias*)node->item;
            if(strcmp(req->path, alias->target_url) == 0)
            {
                size_t file_size;
                char* file_data = load_file(alias->target_file, &file_size);
                if (!file_data)
                {
                    http_response* resp = http_response_init(500, "The requested URL is misconfigured internally, could not find the file path pointed to by target_file.", HTTPRESPONSE_BODYLEN_AUTODETECT);
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

                http_response_add_header(resp, "Content-Type", guess_mime_type(alias->target_file));

                free(file_data);
                return resp;
            }
        }
    }

    // Treat URL as file path

    if (ALLOW_SEARCH)
    {
        char file_path[PATH_MAX];

        if (sanitize_path(req->path, file_path, sizeof(file_path)) == 0)
        {
            struct stat st;

            if (stat(file_path, &st) == 0)
            {
                // Case 1: Regular file
                if (S_ISREG(st.st_mode))
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

                // Case 2: Directory → try index
                if (S_ISDIR(st.st_mode))
                {
                    char index_path[PATH_MAX];

                    if (resolve_index_file(file_path, index_path, sizeof(index_path)) == 0)
                    {
                        size_t file_size;
                        char* file_data = load_file(index_path, &file_size);
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

                        http_response_add_header(resp, "Content-Type", guess_mime_type(index_path));
                        free(file_data);
                        return resp;
                    } else {
                        http_response* resp = http_response_init(404, "Not Found", HTTPRESPONSE_BODYLEN_AUTODETECT);
                        if(!resp)
                            return NULL;
                        http_response_add_header(resp, "Content-Type", "text/plain");
                        return resp;
                    }
                }
            }
        }
    }

    http_response* resp = http_response_init(404, "Not Found", HTTPRESPONSE_BODYLEN_AUTODETECT);
    if(!resp)
        return NULL;
    http_response_add_header(resp, "Content-Type", "text/plain");
    return resp;
}
