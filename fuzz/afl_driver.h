#ifndef SUNSPOTS_AFL_DRIVER_H
#define SUNSPOTS_AFL_DRIVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
#ifdef __cplusplus
}
#endif

#if defined(SUNSPOTS_FUZZ_ENGINE_AFL)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sunspots_fuzz_read_all(FILE *stream, uint8_t **out_buf, size_t *out_size)
{
    uint8_t *buf = NULL;
    size_t cap = 0;
    size_t len = 0;
    uint8_t chunk[4096];

    if (out_buf == NULL || out_size == NULL) {
        return -1;
    }

    while (1) {
        size_t nread = fread(chunk, 1, sizeof(chunk), stream);
        if (nread == 0) {
            if (ferror(stream) != 0) {
                free(buf);
                return -1;
            }
            break;
        }

        if (len + nread > cap) {
            size_t next_cap = cap == 0 ? 4096 : cap;
            while (len + nread > next_cap) {
                next_cap *= 2;
            }
            uint8_t *next_buf = (uint8_t *)realloc(buf, next_cap);
            if (next_buf == NULL) {
                free(buf);
                return -1;
            }
            buf = next_buf;
            cap = next_cap;
        }

        memcpy(buf + len, chunk, nread);
        len += nread;
    }

    *out_buf = buf;
    *out_size = len;
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    FILE *stream = stdin;
    uint8_t *buf = NULL;
    size_t size = 0;

    if (argc > 1) {
        path = argv[1];
        stream = fopen(path, "rb");
        if (stream == NULL) {
            return 1;
        }
    }

    if (sunspots_fuzz_read_all(stream, &buf, &size) != 0) {
        if (stream != stdin) {
            fclose(stream);
        }
        return 1;
    }

    if (stream != stdin) {
        fclose(stream);
    }

    (void)LLVMFuzzerTestOneInput(buf, size);
    free(buf);
    return 0;
}

#endif  /* SUNSPOTS_FUZZ_ENGINE_AFL */

#endif  /* SUNSPOTS_AFL_DRIVER_H */
