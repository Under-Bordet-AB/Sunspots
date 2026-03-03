#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include "linked_list.h"
#include "http_constants.h"

// Reduces leniency on wrongly formatted HTTP requests, safer against fuzzing
#define STRICT_VALIDATION 1

// Auto detect the body length by strlen
#define HTTPRESPONSE_BODYLEN_AUTODETECT -1

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
    LinkedList* query;
    LinkedList* headers;
} http_request;

typedef struct
{
    int responseCode;
    const char* body;
    int bodyLen;
    LinkedList* headers;
} http_response;

typedef struct {
    const char* Name;
    const char* Value;
} http_query_param;

typedef struct {
    char *target_url;
    char *target_file;
} url_alias;

// Attempts to convert a string to a RequestMethod enum, returns Method_Unknown on failure
RequestMethod Enum_Method(const char* method);
// Attempts to convert a string to a ProtocolVersion enum, returns Protocol_Unknown on failure
ProtocolVersion Enum_Protocol(const char* protocol);

// Converts a RequestMethod enum to a string, returns "-unknown-" on failure
const char* RequestMethod_tostring(RequestMethod method);

// Overwrites a string's data making it lowercase
void str_to_lower(char *s);

/*
Searches for a header name in a http_request struct and returns the pointer to its value
Memory is NOT owned by the caller and needs to be handled accordingly
Returns NULL on failure
*/
const char* http_get_header(http_request* req, const char* name);

// Builds a http_request struct from a raw HTTP request message, returns NULL on failure
http_request* http_parse_request(const char* message);
// Safely free up the memory used by a http_request struct
void http_request_dispose(http_request** request_var);

// Helper function for http_request_dispose when freeing http_request->headers, converts void* to http_header*
void http_header_free(void* header_var);

/*
Initialize a new HTTP response struct from a status code and response body.
If body is a null terminated string, bodyLen can be set to `HTTPRESPONSE_BODYLEN_AUTODETECT` and the value will evaluate to strlen(body)
Input body gets copied by the function for safe ownership in the struct and the input can be safely freed by the caller if the intention is to pass it along
Returns NULL on failure
*/
http_response* http_response_init(int code, const char* body, int bodyLen);
// Add a custom response header to the http_response struct by name and value, memory is copied into the struct with strdup
void http_response_add_header(http_response* response, const char* name, const char* value);
/*
Helper function for automatic file serving, evaluates what Content-Type value to send to the requester based off the file extension
Returns "application/octet-stream" on any unknown cases
*/
const char* guess_mime_type(const char *path);
/*
Convert a http_response struct into a raw HTTP response message to send back to a requesting client
outSize is used by the function to write back to the caller informing how large the final message is for safe handling of binary data
Returns NULL on failure
*/
const char* http_response_stringify(http_response* response, size_t* outSize);
// Safely free up the memory used by a http_response struct
void http_response_dispose(http_response** response_var);

#endif