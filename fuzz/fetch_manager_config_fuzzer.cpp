#include <stddef.h>
#include <stdint.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

extern "C" {
int load_apis_from_json(const char *path);
void fetch_manager_reset_apis(void);
}

static int write_all_fd(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t nw = write(fd, buf + off, len - off);
        if (nw < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += (size_t)nw;
    }
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (data == NULL || size > 262144U) {
        return 0;
    }

    std::vector<char> payload(data, data + size);
    for (size_t i = 0; i < payload.size(); ++i) {
        if (payload[i] == '\0') {
            payload[i] = ' ';
        }
    }

    char path_template[] = "/tmp/sunspots_fetch_cfg_XXXXXX.json";
    int fd = mkstemps(path_template, 5);
    if (fd < 0) {
        return 0;
    }

    if (!payload.empty()) {
        (void)write_all_fd(fd, payload.data(), payload.size());
    }
    close(fd);

    (void)load_apis_from_json(path_template);
    fetch_manager_reset_apis();
    unlink(path_template);
    return 0;
}

#include "afl_driver.h"
