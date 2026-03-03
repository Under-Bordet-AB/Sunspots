#include "file_helpers.h"
#include "http_constants.h"

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

char* load_file(const char* path, size_t* out_size)
{
    if (!path)
        return NULL;

    int fd = open(path, O_RDONLY | __O_NOFOLLOW);
    if (fd < 0)
        return NULL;

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return NULL;
    }

    // Must be regular file
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        return NULL;
    }

    // Size validation
    if (st.st_size < 0 || st.st_size == 9223372036854775807) {
        close(fd);
        return NULL;
    }

    size_t size = (size_t)st.st_size;

    char* buffer = malloc(size + 1);
    if (!buffer) {
        close(fd);
        return NULL;
    }

    size_t total_read = 0;
    while (total_read < size) {
        ssize_t r = read(fd, buffer + total_read, size - total_read);
        if (r <= 0) {
            free(buffer);
            close(fd);
            return NULL;
        }
        total_read += (size_t)r;
    }

    buffer[size] = '\0';  // safe for binary
    close(fd);

    if (out_size)
        *out_size = size;

    return buffer;
}

int sanitize_path(const char* url_path, char* out_path, size_t out_size)
{
    // Must start with /
    if (url_path[0] != '/')
        return -1;

    // Reject traversal attempts
    if (strstr(url_path, ".."))
        return -1;

    // Resolve document root
    char doc_root[PATH_MAX];
    if (!realpath(FILE_SEARCH_DIR, doc_root))
        return -1;

    // Build the final path
    char temp[PATH_MAX];
    snprintf(temp, sizeof(temp), "%s%s", doc_root, url_path);

    // Ensure we didn't overflow
    if (strlen(temp) >= PATH_MAX)
        return -1;

    strncpy(out_path, temp, out_size - 1);
    out_path[out_size - 1] = '\0';

    return 0;
}