#ifndef ENDPOINTS_H
#define ENDPOINTS_H

#include "http_parser.h"

http_response* process_request(http_request* req);

#endif