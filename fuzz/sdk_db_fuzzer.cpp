#include <stddef.h>
#include <stdint.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <fcntl.h>
#include <unistd.h>

extern "C" {
#include "sdk/ss_sdk.h"
}

namespace {

std::string g_db_path;

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
        off += (size_t)nw;
    }
    return true;
}

const std::string &fuzz_db_path()
{
    if (!g_db_path.empty()) {
        return g_db_path;
    }

    char tpl[] = "/tmp/sunspots_sdk_db_fuzz_XXXXXX";
    char *dir = mkdtemp(tpl);
    if (dir == NULL) {
        g_db_path = "/tmp/sunspots_sdk_db_fuzz.db";
    } else {
        g_db_path = std::string(dir) + "/db.sqlite";
    }

    (void)setenv("SS_SDK_DB_PATH", g_db_path.c_str(), 1);
    return g_db_path;
}

int64_t align_slot(int64_t ts)
{
    if (ts < 0) {
        return 0;
    }
    return ts - (ts % 900);
}

void write_mutated_blob(const uint8_t *data, size_t size)
{
    const std::string &path = fuzz_db_path();
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd < 0) {
        return;
    }

    const size_t cap = (size > 4096U) ? 4096U : size;
    (void)write_all(fd, data, cap);
    close(fd);
}

ss_sdk_record make_record_from_bytes(const uint8_t *data, size_t size)
{
    ss_sdk_record rec;
    std::memset(&rec, 0, sizeof(rec));

    double value = 0.0;
    if (size >= sizeof(value)) {
        std::memcpy(&value, data, sizeof(value));
        if (!std::isfinite(value)) {
            value = 0.0;
        }
    }

    const int64_t now_utc = (int64_t)time(NULL);
    const int64_t offset_slots = (size > 0U) ? (int64_t)(data[0] % 8U) : 0;
    const int64_t slot = align_slot(now_utc) + offset_slots * 900;

    rec.metric = SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C;
    rec.value_type = SS_SDK_VALUE_F64;
    rec.value.f64 = value;
    rec.ts_start_utc = slot;
    rec.ts_end_utc = slot + 900;
    rec.data_kind = (size > 1U && (data[1] & 1U)) ? SS_SDK_DATA_FORECAST : SS_SDK_DATA_OBSERVATION;

    return rec;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (data == NULL) {
        return 0;
    }

    write_mutated_blob(data, size);

    ss_sdk_samples_out out = {NULL, 0};
    (void)ss_sdk_db_get_canonical(0, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out);
    ss_sdk_db_free_samples(&out);

    const ss_sdk_record rec = make_record_from_bytes(data, size);
    (void)ss_sdk_db_write_record(&rec);

    out.samples = NULL;
    out.count = 0;
    (void)ss_sdk_db_get_canonical(rec.ts_start_utc, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out);
    ss_sdk_db_free_samples(&out);

    return 0;
}

#include "afl_driver.h"
