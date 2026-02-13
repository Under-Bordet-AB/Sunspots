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

bool write_all(int fd, const char *buf, size_t len)
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
        g_db_path = "/tmp/sunspots_sdk_db_fuzz.tsv";
    } else {
        g_db_path = std::string(dir) + "/db.tsv";
    }

    (void)setenv("SS_SDK_DB_PATH", g_db_path.c_str(), 1);
    return g_db_path;
}

void write_mutated_text_rows(const uint8_t *data, size_t size)
{
    const std::string &path = fuzz_db_path();
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd < 0) {
        return;
    }

    std::string text;
    const size_t cap = (size > 4096U) ? 4096U : size;
    text.reserve(cap + 1U);

    for (size_t i = 0; i < cap; ++i) {
        const uint8_t b = data[i];
        char ch = (char)((b % 95U) + 32U);
        if ((b % 17U) == 0U) {
            ch = '\t';
        } else if ((b % 29U) == 0U) {
            ch = '\n';
        }
        text.push_back(ch);
    }

    if (text.empty() || text[text.size() - 1] != '\n') {
        text.push_back('\n');
    }

    (void)write_all(fd, text.data(), text.size());
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

    const int64_t now = (int64_t)time(NULL);
    const int64_t offset = (size > 0U) ? (int64_t)(data[0] % 120U) : 0;

    rec.metric = SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C;
    rec.value_type = SS_SDK_VALUE_F64;
    rec.value.f64 = value;
    rec.ts_start_utc = now - offset;
    rec.ts_end_utc = rec.ts_start_utc + 60;
    rec.data_kind = SS_SDK_DATA_OBSERVATION;
    rec.source_api = "sdk_db_fuzzer";
    rec.source_field = "temperature_2m";
    rec.source_tz = "UTC";
    rec.model_id = "";

    return rec;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (data == NULL) {
        return 0;
    }

    write_mutated_text_rows(data, size);

    ss_sdk_record *rows = NULL;
    size_t count = 0;
    (void)ss_sdk_db_get_last_weeks(8, &rows, &count);
    ss_sdk_db_free_records(rows);

    const ss_sdk_record rec = make_record_from_bytes(data, size);
    (void)ss_sdk_db_write_record(&rec);

    rows = NULL;
    count = 0;
    (void)ss_sdk_db_get_last_weeks(8, &rows, &count);
    ss_sdk_db_free_records(rows);

    return 0;
}

#include "afl_driver.h"
