#include "curl_client.h"

// Global initialization counter to ensure curl_global_init/cleanup are called once
static int global_init_count = 0;

int write_memory_callback(void* contents, size_t size, size_t nmemb, void* user_p) {
    size_t real_size = size * nmemb;

    struct memory_struct* mem = (struct memory_struct*) user_p;

    char* ptr = realloc(mem->memory, mem->size + real_size + 1);
    if (ptr == NULL) {
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->memory[mem->size] = 0;

    return real_size;
}

int curl_client_init(curl_client_t** client) {
    if (!client) {
        return -1;
    }

    // Initialize curl globally if not already done
    if (global_init_count == 0) {
        CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (res != CURLE_OK) {
            return -1;
        }
    }
    global_init_count++;

    // Allocate the client structure
    *client = (curl_client_t*) malloc(sizeof(curl_client_t));
    if (!*client) {
        global_init_count--;
        if (global_init_count == 0) {
            curl_global_cleanup();
        }
        return -1;
    }

    // Initialize all fields to NULL/0
    (*client)->easy_handle = NULL;
    (*client)->multi_handle = NULL;
    (*client)->still_running = 0;
    (*client)->mem.memory = NULL;
    (*client)->mem.size = 0;

    // Initialize easy handle
    (*client)->easy_handle = curl_easy_init();
    if (!(*client)->easy_handle) {
        free(*client);
        *client = NULL;
        global_init_count--;
        if (global_init_count == 0) {
            curl_global_cleanup();
        }
        return -1;
    }

    // Initialize multi handle
    (*client)->multi_handle = curl_multi_init();
    if (!(*client)->multi_handle) {
        curl_easy_cleanup((*client)->easy_handle);
        free(*client);
        *client = NULL;
        global_init_count--;
        if (global_init_count == 0) {
            curl_global_cleanup();
        }
        return -1;
    }

    // Allocate initial memory buffer
    (*client)->mem.memory = malloc(1);
    if (!(*client)->mem.memory) {
        curl_multi_cleanup((*client)->multi_handle);
        curl_easy_cleanup((*client)->easy_handle);
        free(*client);
        *client = NULL;
        global_init_count--;
        if (global_init_count == 0) {
            curl_global_cleanup();
        }
        return -1;
    }
    (*client)->mem.size = 0;

    // Set curl options with error checking
    CURLcode code;
    code = curl_easy_setopt((*client)->easy_handle, CURLOPT_WRITEFUNCTION, write_memory_callback);
    if (code != CURLE_OK) {
        free((*client)->mem.memory);
        curl_multi_cleanup((*client)->multi_handle);
        curl_easy_cleanup((*client)->easy_handle);
        free(*client);
        *client = NULL;
        global_init_count--;
        if (global_init_count == 0) {
            curl_global_cleanup();
        }
        return -1;
    }

    code = curl_easy_setopt((*client)->easy_handle, CURLOPT_WRITEDATA, (void*) &((*client)->mem));
    if (code != CURLE_OK) {
        free((*client)->mem.memory);
        curl_multi_cleanup((*client)->multi_handle);
        curl_easy_cleanup((*client)->easy_handle);
        free(*client);
        *client = NULL;
        global_init_count--;
        if (global_init_count == 0) {
            curl_global_cleanup();
        }
        return -1;
    }

    return 0;
}

int curl_client_make_request(curl_client_t** client, const char* url) {
    if (!client || !*client || !url) {
        return -1;
    }

    CURLcode code = curl_easy_setopt((*client)->easy_handle, CURLOPT_URL, url);
    if (code != CURLE_OK) {
        return -1;
    }

    CURLMcode mc = curl_multi_add_handle((*client)->multi_handle, (*client)->easy_handle);
    if (mc != CURLM_OK) {
        return -1;
    }

    (*client)->still_running = 1;

    return 0;
}

int curl_client_poll(curl_client_t** client) {
    if (!client || !*client) {
        return -1;
    }

    CURLMcode mc = curl_multi_perform((*client)->multi_handle, &(*client)->still_running);
    if (mc != CURLM_OK) {
        return -1;
    }

    return 0;
}

int curl_client_is_running(curl_client_t** client) {
    if (!client || !*client) {
        return -1;
    }

    return (*client)->still_running > 0 ? 1 : 0;
}

int curl_client_read_response(curl_client_t** client, char** buffer) {
    if (!client || !*client || !buffer) {
        return -1;
    }

    if ((*client)->mem.size > 0) {
        *buffer = (char*) malloc((*client)->mem.size + 1);
        if (!*buffer) {
            return -1;
        }
        memcpy(*buffer, (*client)->mem.memory, (*client)->mem.size);
        (*buffer)[(*client)->mem.size] = '\0';
    } else {
        *buffer = NULL;
    }

    return 0;
}

int curl_client_reset(curl_client_t** client) {
    if (!client || !*client) {
        return -1;
    }

    // Remove handle from multi if still added
    if ((*client)->still_running || (*client)->easy_handle) {
        curl_multi_remove_handle((*client)->multi_handle, (*client)->easy_handle);
    }

    // Clear the memory buffer
    if ((*client)->mem.memory) {
        free((*client)->mem.memory);
        (*client)->mem.memory = malloc(1);
        if (!(*client)->mem.memory) {
            return -1;
        }
        (*client)->mem.size = 0;
    }

    (*client)->still_running = 0;

    return 0;
}

int curl_client_cleanup(curl_client_t** client) {
    if (!client || !*client) {
        return -1;
    }

    // Remove easy handle from multi handle if still added
    if ((*client)->easy_handle && (*client)->multi_handle) {
        curl_multi_remove_handle((*client)->multi_handle, (*client)->easy_handle);
    }

    // Free memory buffer
    if ((*client)->mem.memory) {
        free((*client)->mem.memory);
        (*client)->mem.memory = NULL;
    }
    (*client)->mem.size = 0;

    // Cleanup curl handles
    if ((*client)->easy_handle) {
        curl_easy_cleanup((*client)->easy_handle);
        (*client)->easy_handle = NULL;
    }
    if ((*client)->multi_handle) {
        curl_multi_cleanup((*client)->multi_handle);
        (*client)->multi_handle = NULL;
    }

    // Free the client structure
    free(*client);
    *client = NULL;

    // Decrement global counter and cleanup if last instance
    global_init_count--;
    if (global_init_count == 0) {
        curl_global_cleanup();
    }

    return 0;
}
