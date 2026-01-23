#include "curl_client.h"

int write_memory_callback(void* contents, size_t size, size_t nmemb, void* user_p) {
    size_t real_size = size * nmemb;

    struct memory_struct* mem = (struct memory_struct*)user_p;

    char* ptr = realloc(mem->memory, mem->size + real_size + 1);
    if (ptr == NULL) { return 0; }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->memory[mem->size] = 0;

    return real_size;
}

int curl_client_init(curl_client_t** client) {
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) { return -1; }

    (*client)->easy_handle = curl_easy_init();
    if (!(*client)->easy_handle) { return -1; }

    (*client)->multi_handle = curl_multi_init();
    if (!(*client)->multi_handle) {
        curl_easy_cleanup((*client)->easy_handle);
        return -1;
    }

    (*client)->mem.memory = malloc(1);
    if (!(*client)->mem.memory) {
        curl_multi_cleanup((*client)->multi_handle);
        curl_easy_cleanup((*client)->easy_handle);
        return -1;
    }
    (*client)->mem.size = 0;

    curl_easy_setopt((*client)->easy_handle, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt((*client)->easy_handle, CURLOPT_WRITEDATA, (void*)&((*client)->mem));

    return 0;
}

int curl_client_make_request(curl_client_t** client, const char* url) {
    curl_easy_setopt((*client)->easy_handle, CURLOPT_URL, url);
    curl_multi_add_handle((*client)->multi_handle, (*client)->easy_handle);

    return 0;
}

int curl_client_poll(curl_client_t** client) {
    CURLMcode mc = curl_multi_perform((*client)->multi_handle, &(*client)->still_running);
    if (mc != CURLM_OK) { return -1; }

    return 0;
}

int curl_client_read_response(curl_client_t** client, char** buffer) {
    if ((*client)->mem.size > 0) {
        *buffer = (char*)malloc((*client)->mem.size + 1);
        if (!*buffer) { return -1; }
        memcpy(*buffer, (*client)->mem.memory, (*client)->mem.size);
        (*buffer)[(*client)->mem.size] = '\0';
    } else {
        *buffer = NULL;
    }

    return 0;
}

int curl_client_cleanup(curl_client_t** client) {
    if ((*client)->mem.memory) {
        free((*client)->mem.memory);
        (*client)->mem.memory = NULL;
    }
    (*client)->mem.size = 0;

    if ((*client)->easy_handle) {
        curl_easy_cleanup((*client)->easy_handle);
        (*client)->easy_handle = NULL;
    }
    if ((*client)->multi_handle) {
        curl_multi_cleanup((*client)->multi_handle);
        (*client)->multi_handle = NULL;
    }
    curl_global_cleanup();

    return 0;
}
