#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern "C" {
#include "endpoints.h"
#include "http_constants.h"
#include "http_parser.h"
#include "linked_list.h"
}

namespace {

struct FuzzEnv {
    std::string root_dir;
    std::string alias_file;
    std::string served_file;
    std::string index_dir;
    std::string index_file;
    std::string binary_file;
};

FuzzEnv g_env;

bool write_all(int fd, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        const ssize_t nw = write(fd, buf + off, len - off);
        if (nw < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (nw == 0) {
            return false;
        }
        off += static_cast<size_t>(nw);
    }
    return true;
}

bool write_file(const std::string &path, const uint8_t *data, size_t size)
{
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }

    const bool ok = write_all(fd, data, size);
    close(fd);
    return ok;
}

void ensure_dir(const std::string &path)
{
    (void)mkdir(path.c_str(), 0755);
}

const FuzzEnv &fuzz_env()
{
    if (!g_env.root_dir.empty()) {
        return g_env;
    }

    char tpl[] = "/tmp/sunspots_http_fuzz_XXXXXX";
    char *dir = mkdtemp(tpl);
    if (dir == NULL) {
        g_env.root_dir = "/tmp/sunspots_http_fuzz_fallback";
        ensure_dir(g_env.root_dir);
    } else {
        g_env.root_dir = dir;
    }

    g_env.alias_file = g_env.root_dir + "/alias.txt";
    g_env.served_file = g_env.root_dir + "/fuzz.txt";
    g_env.index_dir = g_env.root_dir + "/dir";
    g_env.index_file = g_env.index_dir + "/index.html";
    g_env.binary_file = g_env.root_dir + "/blob.bin";
    ensure_dir(g_env.index_dir);

    static const uint8_t kAliasSeed[] = "alias-seed";
    static const uint8_t kServedSeed[] = "fuzz-seed";
    static const uint8_t kIndexSeed[] = "<html>seed</html>";
    static const uint8_t kBinarySeed[] = {0x00, 0x01, 0x02, 0x03};
    (void)write_file(g_env.alias_file, kAliasSeed, sizeof(kAliasSeed) - 1U);
    (void)write_file(g_env.served_file, kServedSeed, sizeof(kServedSeed) - 1U);
    (void)write_file(g_env.index_file, kIndexSeed, sizeof(kIndexSeed) - 1U);
    (void)write_file(g_env.binary_file, kBinarySeed, sizeof(kBinarySeed));

    FILE_SEARCH_DIR = const_cast<char *>(g_env.root_dir.c_str());
    return g_env;
}

void clear_aliases()
{
    if (URL_ALIASES == NULL) {
        return;
    }

    LinkedList_dispose(&URL_ALIASES, [](void *item) {
        url_alias *alias = static_cast<url_alias *>(item);
        if (alias == NULL) {
            return;
        }
        free(alias->target_url);
        free(alias->target_file);
        free(alias);
    });
}

void add_alias(const std::string &target_url, const std::string &target_file)
{
    if (URL_ALIASES == NULL) {
        URL_ALIASES = LinkedList_create();
        if (URL_ALIASES == NULL) {
            return;
        }
    }

    url_alias *alias = static_cast<url_alias *>(calloc(1, sizeof(url_alias)));
    if (alias == NULL) {
        return;
    }

    alias->target_url = strdup(target_url.c_str());
    alias->target_file = strdup(target_file.c_str());
    if (alias->target_url == NULL || alias->target_file == NULL) {
        free(alias->target_url);
        free(alias->target_file);
        free(alias);
        return;
    }

    str_to_lower(alias->target_url);
    if (LinkedList_append(URL_ALIASES, alias) != 0) {
        free(alias->target_url);
        free(alias->target_file);
        free(alias);
    }
}

std::string safe_token(const uint8_t *data, size_t size, size_t offset, size_t max_len)
{
    std::string out;
    if (data == NULL || size == 0U || offset >= size) {
        return out;
    }

    const size_t end = std::min(size, offset + max_len);
    for (size_t i = offset; i < end; ++i) {
        const unsigned char ch = data[i];
        if (std::isalnum(ch) != 0) {
            out.push_back(static_cast<char>(std::tolower(ch)));
        } else if (ch == '-' || ch == '_' || ch == '.') {
            out.push_back(static_cast<char>(ch));
        }
    }
    if (out.empty()) {
        out = "x";
    }
    return out;
}

void refresh_files(const uint8_t *data, size_t size)
{
    const FuzzEnv &env = fuzz_env();
    const size_t cap = std::min<size_t>(size, 512U);
    const uint8_t *payload = data;
    size_t payload_size = cap;
    if (payload == NULL || payload_size == 0U) {
        static const uint8_t kEmpty = '\n';
        payload = &kEmpty;
        payload_size = 1U;
    }

    (void)write_file(env.alias_file, payload, payload_size);
    (void)write_file(env.served_file, payload, payload_size);
    (void)write_file(env.index_file, payload, payload_size);
    (void)write_file(env.binary_file, payload, payload_size);
}

std::string build_structured_request(const uint8_t *data, size_t size)
{
    static const char *kMethods[] = {"GET", "POST", "PUT", "DELETE", "PATCH", "OPTIONS", "HEAD", "BAD"};
    static const char *kProtocols[] = {"HTTP/1.1", "HTTP/1.0", "HTTP/2.0", "HTTP/9.9"};
    static const char *kPaths[] = {
        "/",
        "/health",
        "/alias",
        "/fuzz.txt",
        "/dir/",
        "/blob.bin",
        "/../etc/passwd",
        "/missing"
    };

    const uint8_t b0 = size > 0U ? data[0] : 0U;
    const uint8_t b1 = size > 1U ? data[1] : 0U;
    const uint8_t b2 = size > 2U ? data[2] : 0U;
    const uint8_t b3 = size > 3U ? data[3] : 0U;

    std::string request = kMethods[b0 % (sizeof(kMethods) / sizeof(kMethods[0]))];
    request += " ";
    request += kPaths[b1 % (sizeof(kPaths) / sizeof(kPaths[0]))];

    if ((b2 & 1U) != 0U) {
        request += "?";
        request += safe_token(data, size, 4U, 16U);
        if ((b2 & 2U) != 0U) {
            request += "=";
            request += safe_token(data, size, 20U, 24U);
        }
        if ((b2 & 4U) != 0U) {
            request += "&flag";
        }
    }

    request += " ";
    request += kProtocols[b3 % (sizeof(kProtocols) / sizeof(kProtocols[0]))];
    request += "\r\n";

    if (size > 4U) {
        request += "Host: ";
        request += safe_token(data, size, 5U, 24U);
        request += "\r\n";
    }
    if (size > 8U) {
        request += "X-Fuzz: ";
        request += safe_token(data, size, 9U, 32U);
        request += "\r\n";
    }
    if ((b0 & 1U) != 0U) {
        request += "Content-Length: ";
        request += safe_token(data, size, 12U, 4U);
        request += "\r\n";
    }

    request += "\r\n";
    if ((b1 & 1U) != 0U && size > 16U) {
        request.append(reinterpret_cast<const char *>(data + 16U), std::min<size_t>(size - 16U, 64U));
    }

    return request;
}

std::string build_request_buffer(const uint8_t *data, size_t size)
{
    if (size > 0U && (data[0] & 0x80U) != 0U) {
        return build_structured_request(data, size);
    }

    std::string raw;
    if (data != NULL && size > 0U) {
        raw.assign(reinterpret_cast<const char *>(data), size);
    }
    return raw;
}

void configure_runtime(const uint8_t *data, size_t size)
{
    const FuzzEnv &env = fuzz_env();
    refresh_files(data, size);
    clear_aliases();

    ALLOW_SEARCH = (size == 0U || data[0] % 3U != 0U) ? 1 : 0;
    FILE_SEARCH_DIR = const_cast<char *>(env.root_dir.c_str());

    add_alias("/alias", ((size > 1U) && (data[1] & 1U)) ? env.alias_file : env.served_file);
    if (size > 2U && (data[2] & 1U)) {
        add_alias("/missing-alias", env.root_dir + "/does-not-exist.txt");
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    configure_runtime(data, size);

    const std::string request_buf = build_request_buffer(data, size);
    std::vector<char> nul_terminated(request_buf.begin(), request_buf.end());
    nul_terminated.push_back('\0');

    http_request *req = http_parse_request(nul_terminated.data());
    if (req != NULL) {
        http_response *resp = process_request(req);
        if (resp != NULL) {
            size_t out_size = 0;
            const char *wire = http_response_stringify(resp, &out_size);
            free(const_cast<char *>(wire));
            http_response_dispose(&resp);
        }

        if (req->headers != NULL) {
            (void)http_get_header(req, "host");
        }
        http_request_dispose(&req);
    }

    clear_aliases();
    return 0;
}

#include "afl_driver.h"
