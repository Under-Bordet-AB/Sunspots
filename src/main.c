#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "fetch/curly.h"

#define url_mock "http://example.com"
#define url_httpbin "http://httpbin.org/get"

int main() {
    printf("Curly starting...\n");

    curly_t* curl_client;
    curly_init(&curl_client);

    curly_make_request(&curl_client, url_httpbin);

    while (curly_poll(&curl_client) == 0) {
        printf("curling\n");

        if (curly_is_running(&curl_client) == 0) {
            break;
        }
    }

    char* buffer = NULL;
    curly_read_response(&curl_client, &buffer);

    printf("%s\n", buffer);

    free(buffer);

    curly_cleanup(&curl_client);

    return 0;
}
