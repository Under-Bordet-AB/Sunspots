#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include "libs/linked_list/linked_list.h"
#include "frontend/http_constants.h"

#define STRICT_VALIDATION 1

typedef enum {
    Method_Unknown = 0,

    GET = 1,
    POST = 2,
    PUT = 3,
    DELETE = 4,
    PATCH = 5,
    OPTIONS = 6,
    HEAD = 7
} RequestMethod;

typedef enum {
    Protocol_Unknown = 0,

    HTTP_0_9 = 1,
    HTTP_1_0 = 2,
    HTTP_1_1 = 3,
    HTTP_2_0 = 4,
    HTTP_3_0 = 5
} ProtocolVersion;

typedef struct {
    const char* Name;
    const char* Value;
} http_header;

typedef struct
{
    RequestMethod method;
    ProtocolVersion protocol;
    const char* path;
    LinkedList* headers;
} http_request;

typedef struct
{
    int responseCode;
    const char* body;
    int bodyLen;
    LinkedList* headers;
} http_response;

RequestMethod Enum_Method(const char* method);
ProtocolVersion Enum_Protocol(const char* protocol);

const char* RequestMethod_tostring(RequestMethod method);

void str_to_lower(char *s);

const char* http_get_header(http_request* req, const char* name);

http_request* http_parse_request(const char* message);
void http_request_dispose(http_request** request_var);

void http_header_free(void* header_var);

http_response* http_response_init(int code, const char* body, int bodyLen);
void http_response_add_header(http_response* response, const char* name, const char* value);
const char* http_response_stringify(http_response* response, size_t* outSize);
void http_response_dispose(http_response** response_var);

#endif