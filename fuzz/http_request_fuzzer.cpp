#include <stddef.h>
#include <stdint.h>

#include <stdlib.h>
#include <vector>

extern "C" {
#include "endpoints.h"
#include "http_parser.h"
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (data == NULL || size > 65536U) {
        return 0;
    }

    std::vector<char> request(data, data + size);
    request.push_back('\0');

    http_request *parsed = http_parse_request(request.data());
    if (parsed == NULL) {
        return 0;
    }

    (void)http_get_header(parsed, "host");

    http_response *response = process_request(parsed);
    if (response != NULL) {
        size_t wire_len = 0;
        const char *wire = http_response_stringify(response, &wire_len);
        (void)wire_len;
        free((void *)wire);
        http_response_dispose(&response);
    }

    http_request_dispose(&parsed);
    return 0;
}

#include "afl_driver.h"
