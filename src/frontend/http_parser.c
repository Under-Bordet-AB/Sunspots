#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "libs/linked_list/linked_list.h"
#include "frontend/http_parser.h"

char* substr(const char* start, const char* end)
{
    return strndup(start, end-start);
}

RequestMethod Enum_Method(const char* method) {
    if (!method) return Method_Unknown;

    if (strcmp(method, "GET") == 0) return GET;
    if (strcmp(method, "POST") == 0) return POST;
    if (strcmp(method, "PUT") == 0) return PUT;
    if (strcmp(method, "DELETE") == 0) return DELETE;
    if (strcmp(method, "PATCH") == 0) return PATCH;
    if (strcmp(method, "OPTIONS") == 0) return OPTIONS;
    if (strcmp(method, "HEAD") == 0) return HEAD;

    return Method_Unknown;
}

ProtocolVersion Enum_Protocol(const char* protocol) {
    if (!protocol) return Protocol_Unknown;

    if (strcmp(protocol, "HTTP/0.9") == 0) return HTTP_0_9;
    if (strcmp(protocol, "HTTP/1.0") == 0) return HTTP_1_0;
    if (strcmp(protocol, "HTTP/1.1") == 0) return HTTP_1_1;
    if (strcmp(protocol, "HTTP/2.0") == 0) return HTTP_2_0;
    if (strcmp(protocol, "HTTP/3.0") == 0) return HTTP_3_0;

    return Protocol_Unknown;
}

void str_to_lower(char *s)
{
    if (!s) return;
    for (; *s; ++s)
        *s = (char)tolower((unsigned char)*s);
}

const char* http_get_header(http_request* req, const char* name)
{
    char* nameLow = strdup(name);
    str_to_lower(nameLow);
    LinkedList_foreach(req->headers, node)
    {
        http_header* hdr = (http_header*)node->item;
        if(strcmp(hdr->Name, nameLow)==0)
        {
            free(nameLow);
            return hdr->Value;
        }
    };
    free(nameLow);
    return NULL;
}

http_request* http_parse_request(const char* message) {
    http_request* request = calloc(1, sizeof(http_request));
    if(request == NULL)
        return NULL;

    int state = 0;

    const char* start = message;
    int finalLoop = 0;
    while (start && *start && !finalLoop) {
        // Find end of line
        const char* end = strstr(start, "\r\n");
        if (!end) {
            // No separator found, read to end of message and do not loop again
            finalLoop = 1;
            end = message + strlen(message);
        }
        // Check length with pointer math
        int length = end - start;
        if (length == 0) {
            // printf("Reached end of request.\n\n");
            if(state==1)
            {
                break;
            } else {
                http_request_dispose(&request);
                return NULL;
            }
        }
        // Allocate memory for current line
        char* current_line = substr(start, end);
        if (!current_line) // horrible problem
        {
            http_request_dispose(&request);
            return NULL;
        }

        if (state == 0) {
            // Count spaces in request, should match 2
            int count = 0;
            char* scan = current_line;
            for (; *scan; scan++) {
                if (*scan == ' ') count++;
            }
            if (count != 2) {
                //printf("INVALID: Request is not formatted with 2 spaces.\n\n");
                free(current_line);
                http_request_dispose(&request);
                return NULL;
            }

            const char* space1 = strchr(current_line, ' ');
            if(!space1)
            {
                free(current_line);
                http_request_dispose(&request);
                return NULL;
            }
            const char* space2 = strchr(space1 + 1, ' ');

            if (!space2 || space2 <= space1)
            {
                //request->reason = Malformed;
                free(current_line);
                http_request_dispose(&request);
                return NULL;
            }
            if (space2 - (space1 + 1) >= MAX_URL_LEN) {
                //printf("INVALID: Request URL is too long\n\n");
                //request->reason = URLTooLong;
                free(current_line);
                http_request_dispose(&request);
                return NULL;
            }

            char* method = substr(current_line, space1);
            if (!method) {
                free(current_line);
                http_request_dispose(&request);
                //request->reason = OutOfMemory;
                return NULL;
            }
            char* path = substr(space1 + 1, space2);
            if (!path) {
                free(method);
                free(current_line);
                http_request_dispose(&request);
                //request->reason = OutOfMemory;
                return NULL;
            }
            char* protocol = substr(space2 + 1, current_line + length);
            if (!protocol) {
                free(method); free(path);
                free(current_line);
                http_request_dispose(&request);
                //request->reason = OutOfMemory;
                return NULL;
            }

            request->method = Enum_Method(method);
            if(STRICT_VALIDATION && request->method == Method_Unknown)
            {
                free(method); free(path); free(protocol);
                free(current_line);
                http_request_dispose(&request);
                //request->reason = InvalidMethod;
                return NULL;
            }
            request->protocol = Enum_Protocol(protocol);
            if(STRICT_VALIDATION && request->protocol == Protocol_Unknown)
            {
                free(method); free(path); free(protocol);
                free(current_line);
                http_request_dispose(&request);
                //request->reason = InvalidProtocol;
                return NULL;
            }
            request->path = path;
            if(STRICT_VALIDATION && path[0] != '/')
            {
                free(method); free(protocol);
                free(current_line);
                http_request_dispose(&request);
                //request->reason = InvalidURL;
                return NULL;
            }

            free(method);
            free(protocol);

            //request->valid = 1;
            //request->reason = NotInvalid;
            state = 1; // jump to header parsing
            request->headers = LinkedList_create();
            if(request->headers == NULL)
            {
                free(current_line);
                free(request);
                return NULL;
            }
        } else {
            const char* sep = strstr(current_line, ": ");
            if (!sep) {
                //printf("INVALID: Header is malformed.\n\n");
                free(current_line);
                http_request_dispose(&request);
                return NULL;
            }

            char* name = substr(current_line, sep);
            if (!name) {
                free(current_line);
                http_request_dispose(&request);
                return NULL;
            }
            str_to_lower(name);
            char* value = substr(sep + 2, current_line + length);
            if (!value) {
                free(name);
                free(current_line);
                http_request_dispose(&request);
                return NULL;
            }

            http_header* header = calloc(1, sizeof(http_header));
            if (header != NULL) {
                header->Name = name;
                header->Value = value;
                LinkedList_append(request->headers, header);
            } else {
                free(name);
                free(value);
                free(current_line);
                http_request_dispose(&request);
                return NULL;
            }
        }

        start = end + 2;
        free(current_line);
    }

    return request;
}

void http_request_dispose(http_request** request_var)
{
    if (request_var && *request_var) {
        http_request* request = *request_var;
        if(request->path != NULL)
            free((void*)request->path);
        if(request->headers != NULL)
            LinkedList_dispose(&request->headers, http_header_free);
        free(request);
        *request_var = NULL;
    }
}

void http_header_free(void* header_var) {
    if(header_var)
    {
        http_header* hdr = (http_header*)header_var;
        free((void*)hdr->Name);
        free((void*)hdr->Value);
        free(hdr);
    }
}