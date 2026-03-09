#include <string.h>
#include <time.h>
#include <syslog.h>
#include <curl/curl.h>
#include "../libs/json/cJSON.h"

typedef struct {
    char* data;
    size_t size;
} memory;

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    memory* mem = (memory*)userp;

    char* ptr = realloc(mem->data, mem->size + realsize + 1);
    if (ptr == NULL) {
        return 0;
    }

    mem->data = ptr;

    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

int fetch_from_url(char* url, char** buffer, int timeout) {
    CURL* curl;
    CURLcode res;
    memory chunk = {0};

    if (url == NULL || buffer == NULL || timeout <= 0) {
        return -1;
    }

    *buffer = NULL;

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        syslog(LOG_WARNING, "curl_global_init failed.");
        return -1;
    }

    curl = curl_easy_init();
    if (curl == NULL) {
        syslog(LOG_WARNING, "curl_easy_init failed.");
        curl_global_cleanup();
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        syslog(LOG_WARNING, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        free(chunk.data);
        return -1;
    }

    
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    
    *buffer = chunk.data;
    return 0;
}