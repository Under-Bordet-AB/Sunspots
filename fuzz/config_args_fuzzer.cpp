#include <stddef.h>
#include <stdint.h>

#include <ctype.h>

#include <string>
#include <vector>

extern "C" {
#include "config.h"
}

static std::string sanitize_key(const uint8_t *bytes, size_t len)
{
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        const unsigned char ch = bytes[i];
        if (isalnum(ch) != 0 || ch == '.' || ch == '_' || ch == '-') {
            out.push_back((char)ch);
        }
    }
    if (out.empty()) {
        out = "k";
    }
    return out;
}

static std::string sanitize_value(const uint8_t *bytes, size_t len)
{
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        const unsigned char ch = bytes[i];
        if (isprint(ch) != 0 && ch != '\\') {
            out.push_back((char)ch);
        }
    }
    if (out.empty()) {
        out = "0";
    }
    return out;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    config *cfg = config_create();
    if (cfg == NULL) {
        return 0;
    }

    std::vector<std::string> storage;
    storage.reserve(33);
    storage.push_back("config_args_fuzzer");

    size_t i = 0;
    unsigned int pair_count = 0;
    while (i < size && pair_count < 16U) {
        size_t key_len = (size_t)(data[i] % 12U) + 1U;
        ++i;
        if (i + key_len > size) {
            key_len = size - i;
        }
        std::string key = sanitize_key(data + i, key_len);
        i += key_len;

        size_t value_len = 0;
        if (i < size) {
            value_len = (size_t)(data[i] % 24U);
            ++i;
            if (i + value_len > size) {
                value_len = size - i;
            }
        }
        std::string value = sanitize_value(data + i, value_len);
        i += value_len;

        storage.push_back("--" + key);
        storage.push_back(value);
        ++pair_count;
    }

    if (storage.size() == 1U) {
        storage.push_back("--debug");
        storage.push_back("false");
    }

    std::vector<char *> argv;
    argv.reserve(storage.size());
    for (size_t j = 0; j < storage.size(); ++j) {
        argv.push_back((char *)storage[j].c_str());
    }

    (void)config_load_args(cfg, (int)argv.size(), argv.data());
    (void)config_get_subtree(cfg, "modules");
    (void)config_get_int_or(cfg, "server.port", 8080);
    (void)config_get_bool_or(cfg, "debug", false);
    (void)config_get_string_or(cfg, "host", "localhost");

    char host_buf[64];
    (void)config_get_string(cfg, "host", host_buf, sizeof(host_buf));

    config_destroy(&cfg);
    return 0;
}

#include "afl_driver.h"
