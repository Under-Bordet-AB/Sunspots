#include <curl/curl.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "providers/smhi_provider.h"
#include "sdk/sunspots_sdk.h"

static volatile sig_atomic_t g_running = 1;

static void on_term(int sig) {
    (void) sig;
    g_running = 0;
}

static int read_file(const char* path, char* out, size_t out_sz) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return -errno;
    }
    size_t n = fread(out, 1, out_sz - 1, f);
    if (ferror(f)) {
        (void) fclose(f);
        return -EIO;
    }
    out[n] = '\0';
    (void) fclose(f);
    return (int) n;
}

typedef struct curl_fixed_buffer {
    char* out;
    size_t out_sz;
    size_t total;
    bool overflow;
} curl_fixed_buffer;

static size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    curl_fixed_buffer* b = (curl_fixed_buffer*) userdata;
    size_t in_sz = size * nmemb;
    if (!b || !ptr || in_sz == 0) {
        return in_sz;
    }

    size_t space = (b->out_sz > b->total + 1) ? (b->out_sz - b->total - 1) : 0;
    size_t copy_n = (in_sz < space) ? in_sz : space;
    if (copy_n > 0) {
        memcpy(b->out + b->total, ptr, copy_n);
        b->total += copy_n;
    }
    if (copy_n < in_sz) {
        b->overflow = true;
    }
    return in_sz;
}

static int fetch_text(CURL* easy, const char* url, char* out, size_t out_sz) {
    if (!url || !out || out_sz < 2) {
        return -EINVAL;
    }
    if (strncmp(url, "file://", 7) == 0) {
        return read_file(url + 7, out, out_sz);
    }

    if (!easy) {
        return -EINVAL;
    }
    curl_fixed_buffer buf = {.out = out, .out_sz = out_sz, .total = 0, .overflow = false};
    out[0] = '\0';

    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &buf);
    CURLcode rc = curl_easy_perform(easy);
    if (rc != CURLE_OK) {
        return -EIO;
    }
    if (buf.overflow) {
        return -EOVERFLOW;
    }
    out[buf.total] = '\0';
    return (int) buf.total;
}

int main(int argc, char** argv) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    sssdk_runtime rt;
    if (sssdk_bootstrap(&rt, argc, argv) != 0) {
        return EXIT_FAILURE;
    }

    char api_url[1024];
    if (config_get_string(rt.worker, "api_url", api_url, sizeof(api_url)) != 0) {
        char latitude[64];
        char longitude[64];
        char api_base[512];
        if (config_get_string(rt.common, "location.latitude", latitude, sizeof(latitude)) != 0 ||
            config_get_string(rt.common, "location.longitude", longitude, sizeof(longitude)) != 0 ||
            config_get_string(rt.worker, "api_base", api_base, sizeof(api_base)) != 0) {
            (void) sssdk_shutdown(&rt);
            return EXIT_FAILURE;
        }
        int n = snprintf(api_url, sizeof(api_url), "%s/lon/%s/lat/%s/data.json", api_base, longitude, latitude);
        if (n <= 0 || (size_t) n >= sizeof(api_url)) {
            (void) sssdk_shutdown(&rt);
            return EXIT_FAILURE;
        }
    }

    if (api_url[0] == '\0') {
        (void) sssdk_shutdown(&rt);
        return EXIT_FAILURE;
    }

    int poll_ms = sssdk_get_poll_interval_ms(&rt, rt.heartbeat_ms);
    bool run_once = config_get_bool_or(rt.worker, "run_once", false);
    if (poll_ms > 0) {
        rt.heartbeat_ms = poll_ms;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        (void) sssdk_shutdown(&rt);
        return EXIT_FAILURE;
    }
    CURL* easy = curl_easy_init();
    if (!easy) {
        curl_global_cleanup();
        (void) sssdk_shutdown(&rt);
        return EXIT_FAILURE;
    }
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, "sunspots-fetcher/1.0");
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, curl_write_cb);

    char body[262144];
    while (g_running && sssdk_should_run(&rt)) {
        int f_rc = fetch_text(easy, api_url, body, sizeof(body));
        if (f_rc > 0) {
            (void) smhi_emit_from_json(&rt, body, 0);
        } else {
            (void) sssdk_log_warn(&rt, "smhi fetch failed");
        }
        (void) sssdk_heartbeat(&rt);
        if (run_once) {
            break;
        }
        (void) sssdk_sleep_interval(&rt);
    }

    curl_easy_cleanup(easy);
    curl_global_cleanup();
    (void) sssdk_shutdown(&rt);
    return EXIT_SUCCESS;
}
