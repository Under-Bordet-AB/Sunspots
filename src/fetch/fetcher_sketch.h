#ifndef FETCHER_H
#define FETCHER_H

typedef struct fetcher_t {
    char* api_name;
    char* api_url;
    int interval;
    void* normalize_function;
} fetcher_t;

int fetch_from_url(fetcher_t** fetcher);

#endif