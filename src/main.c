#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "fetch/curl_client.h"

#define url_mock "http://example.com"
#define url_httpbin "http://httpbin.org/get"

int main() {
    printf("Curl on that thang\n");

    curl_client_t* curl_client;
    curl_client_init(&curl_client);

    curl_client_make_request(&curl_client, url_httpbin);

    while (curl_client_poll(&curl_client) == 0) {
        printf("curling\n");

        if (curl_client_is_running(&curl_client) == 0) {
            break;
        }
    }

    char* buffer = NULL;
    curl_client_read_response(&curl_client, &buffer);

    printf("%s\n", buffer);

    free(buffer);

    curl_client_cleanup(&curl_client);

    return 0;
}
