#include <gtest/gtest.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "sdk/ss_sdk.h"
}

namespace {

class ScopedEnvVar {
public:
    explicit ScopedEnvVar(const char *name) : name_(name), had_old_(false)
    {
        const char *old = std::getenv(name_);
        if (old != NULL) {
            had_old_ = true;
            old_value_ = old;
        }
    }

    ~ScopedEnvVar()
    {
        if (had_old_) {
            setenv(name_, old_value_.c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }

private:
    const char *name_;
    bool had_old_;
    std::string old_value_;
};

std::string make_temp_dir()
{
    char tpl[] = "/tmp/sunspots_sdk_db_test_XXXXXX";
    char *dir = mkdtemp(tpl);
    if (dir == NULL) {
        return "";
    }
    return std::string(dir);
}

void remove_file_if_exists(const std::string &path)
{
    (void)unlink(path.c_str());
}

void remove_dir_if_exists(const std::string &path)
{
    (void)rmdir(path.c_str());
}

std::string read_text_file(const std::string &path)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return "";
    }

    std::string out;
    char buf[512];
    for (;;) {
        const ssize_t nr = read(fd, buf, sizeof(buf));
        if (nr < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return "";
        }
        if (nr == 0) {
            break;
        }
        out.append(buf, (size_t)nr);
    }

    close(fd);
    return out;
}

int append_text_file(const std::string &path, const std::string &data)
{
    int fd = open(path.c_str(), O_WRONLY | O_APPEND);
    if (fd < 0) {
        return -1;
    }

    size_t off = 0;
    while (off < data.size()) {
        const ssize_t nw = write(fd, data.data() + off, data.size() - off);
        if (nw < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return -1;
        }
        if (nw == 0) {
            close(fd);
            errno = EIO;
            return -1;
        }
        off += (size_t)nw;
    }

    close(fd);
    return 0;
}

ss_sdk_record make_base_record(double value, int64_t ts_start, int64_t ts_end, const char *source_field)
{
    ss_sdk_record rec;
    std::memset(&rec, 0, sizeof(rec));
    rec.metric = SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C;
    rec.value_type = SS_SDK_VALUE_F64;
    rec.value.f64 = value;
    rec.ts_start_utc = ts_start;
    rec.ts_end_utc = ts_end;
    rec.data_kind = SS_SDK_DATA_OBSERVATION;
    rec.source_api = "openmeteo";
    rec.source_field = source_field;
    rec.source_tz = "UTC";
    rec.model_id = "";
    return rec;
}

}  // namespace

TEST(sdk_db_module, dedupe_identity_keeps_distinct_records_and_dedupes_exact_duplicates)
{
    ScopedEnvVar db_path_guard("SS_SDK_DB_PATH");

    const std::string dir = make_temp_dir();
    ASSERT_FALSE(dir.empty());
    const std::string db_path = dir + "/sdk_db.tsv";
    setenv("SS_SDK_DB_PATH", db_path.c_str(), 1);

    const int64_t now = (int64_t)time(NULL);
    const ss_sdk_record rec_a = make_base_record(4.25, now - 120, now - 60, "temperature_2m");
    const ss_sdk_record rec_b = make_base_record(5.75, now - 120, now - 30, "temperature_2m_alt");

    EXPECT_EQ(ss_sdk_db_write_record(&rec_a), SS_SDK_OK);
    EXPECT_EQ(ss_sdk_db_write_record(&rec_b), SS_SDK_OK);
    EXPECT_EQ(ss_sdk_db_write_record(&rec_a), SS_SDK_OK);

    ss_sdk_record *rows = NULL;
    size_t count = 0;
    ASSERT_EQ(ss_sdk_db_get_last_weeks(8, &rows, &count), SS_SDK_OK);
    ASSERT_EQ(count, (size_t)2);

    bool saw_a = false;
    bool saw_b = false;
    for (size_t i = 0; i < count; ++i) {
        if (std::strcmp(rows[i].source_field, "temperature_2m") == 0) {
            saw_a = true;
        }
        if (std::strcmp(rows[i].source_field, "temperature_2m_alt") == 0) {
            saw_b = true;
        }
    }
    EXPECT_TRUE(saw_a);
    EXPECT_TRUE(saw_b);
    ss_sdk_db_free_records(rows);

    remove_file_if_exists(db_path);
    remove_dir_if_exists(dir);
}

TEST(sdk_db_module, rejects_non_finite_f64_values)
{
    ScopedEnvVar db_path_guard("SS_SDK_DB_PATH");

    const std::string dir = make_temp_dir();
    ASSERT_FALSE(dir.empty());
    const std::string db_path = dir + "/sdk_db.tsv";
    setenv("SS_SDK_DB_PATH", db_path.c_str(), 1);

    const int64_t now = (int64_t)time(NULL);
    ss_sdk_record rec = make_base_record(1.0, now - 120, now - 60, "temperature_2m");

    rec.value.f64 = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_ERR_VALIDATION);

    rec.value.f64 = std::numeric_limits<double>::infinity();
    EXPECT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_ERR_VALIDATION);

    rec.value.f64 = -std::numeric_limits<double>::infinity();
    EXPECT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_ERR_VALIDATION);

    remove_file_if_exists(db_path);
    remove_dir_if_exists(dir);
}

TEST(sdk_db_module, reader_skips_corrupted_enum_rows)
{
    ScopedEnvVar db_path_guard("SS_SDK_DB_PATH");

    const std::string dir = make_temp_dir();
    ASSERT_FALSE(dir.empty());
    const std::string db_path = dir + "/sdk_db.tsv";
    setenv("SS_SDK_DB_PATH", db_path.c_str(), 1);

    const int64_t now = (int64_t)time(NULL);
    const ss_sdk_record rec = make_base_record(10.5, now - 120, now - 60, "temperature_2m");
    ASSERT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_OK);

    const std::string bad_metric =
        "999999\t1\t0x4025000000000000\t" + std::to_string(now - 120) +
        "\t" + std::to_string(now - 60) + "\t0\tapi\tfield\tUTC\t\t0\t0\n";
    const std::string bad_value_type =
        "0\t99\t1\t" + std::to_string(now - 120) +
        "\t" + std::to_string(now - 60) + "\t0\tapi\tfield\tUTC\t\t0\t0\n";
    const std::string bad_data_kind =
        "0\t1\t0x4025000000000000\t" + std::to_string(now - 120) +
        "\t" + std::to_string(now - 60) + "\t99\tapi\tfield\tUTC\t\t0\t0\n";

    ASSERT_EQ(append_text_file(db_path, bad_metric), 0);
    ASSERT_EQ(append_text_file(db_path, bad_value_type), 0);
    ASSERT_EQ(append_text_file(db_path, bad_data_kind), 0);

    ss_sdk_record *rows = NULL;
    size_t count = 0;
    ASSERT_EQ(ss_sdk_db_get_last_weeks(8, &rows, &count), SS_SDK_OK);
    EXPECT_EQ(count, (size_t)1);
    ss_sdk_db_free_records(rows);

    remove_file_if_exists(db_path);
    remove_dir_if_exists(dir);
}

TEST(sdk_db_module, writes_f64_values_in_hex_bit_format)
{
    ScopedEnvVar db_path_guard("SS_SDK_DB_PATH");

    const std::string dir = make_temp_dir();
    ASSERT_FALSE(dir.empty());
    const std::string db_path = dir + "/sdk_db.tsv";
    setenv("SS_SDK_DB_PATH", db_path.c_str(), 1);

    const int64_t now = (int64_t)time(NULL);
    const ss_sdk_record rec = make_base_record(12.625, now - 120, now - 60, "temperature_2m");

    ASSERT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_OK);

    const std::string body = read_text_file(db_path);
    ASSERT_FALSE(body.empty());
    EXPECT_NE(body.find("\t0x"), std::string::npos);

    remove_file_if_exists(db_path);
    remove_dir_if_exists(dir);
}

TEST(sdk_db_module, reads_legacy_decimal_f64_rows_for_backwards_compatibility)
{
    ScopedEnvVar db_path_guard("SS_SDK_DB_PATH");

    const std::string dir = make_temp_dir();
    ASSERT_FALSE(dir.empty());
    const std::string db_path = dir + "/sdk_db.tsv";
    setenv("SS_SDK_DB_PATH", db_path.c_str(), 1);

    const int64_t now = (int64_t)time(NULL);
    const std::string legacy_line =
        "0\t1\t12.5\t" + std::to_string(now - 120) +
        "\t" + std::to_string(now - 60) + "\t0\tlegacy\ttemp\tUTC\t\t0\t0\n";

    int fd = open(db_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0664);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(write(fd, legacy_line.data(), legacy_line.size()), (ssize_t)legacy_line.size());
    close(fd);

    ss_sdk_record *rows = NULL;
    size_t count = 0;
    ASSERT_EQ(ss_sdk_db_get_last_weeks(8, &rows, &count), SS_SDK_OK);
    ASSERT_EQ(count, (size_t)1);
    EXPECT_EQ(rows[0].value_type, SS_SDK_VALUE_F64);
    EXPECT_DOUBLE_EQ(rows[0].value.f64, 12.5);
    ss_sdk_db_free_records(rows);

    remove_file_if_exists(db_path);
    remove_dir_if_exists(dir);
}

TEST(sdk_db_module, reader_skips_rows_with_metric_value_type_mismatch)
{
    ScopedEnvVar db_path_guard("SS_SDK_DB_PATH");

    const std::string dir = make_temp_dir();
    ASSERT_FALSE(dir.empty());
    const std::string db_path = dir + "/sdk_db.tsv";
    setenv("SS_SDK_DB_PATH", db_path.c_str(), 1);

    const int64_t now = (int64_t)time(NULL);
    const ss_sdk_record valid = make_base_record(9.5, now - 120, now - 60, "temperature_2m");
    ASSERT_EQ(ss_sdk_db_write_record(&valid), SS_SDK_OK);

    const std::string bad_metric_type =
        "0\t0\t42\t" + std::to_string(now - 120) +
        "\t" + std::to_string(now - 60) + "\t0\tlegacy\tbad_metric_type\tUTC\t\t0\t0\n";
    ASSERT_EQ(append_text_file(db_path, bad_metric_type), 0);

    ss_sdk_record *rows = NULL;
    size_t count = 0;
    ASSERT_EQ(ss_sdk_db_get_last_weeks(8, &rows, &count), SS_SDK_OK);
    ASSERT_EQ(count, (size_t)1);
    EXPECT_STREQ(rows[0].source_field, "temperature_2m");
    ss_sdk_db_free_records(rows);

    remove_file_if_exists(db_path);
    remove_dir_if_exists(dir);
}

TEST(sdk_db_module, reader_skips_non_finite_f64_rows)
{
    ScopedEnvVar db_path_guard("SS_SDK_DB_PATH");

    const std::string dir = make_temp_dir();
    ASSERT_FALSE(dir.empty());
    const std::string db_path = dir + "/sdk_db.tsv";
    setenv("SS_SDK_DB_PATH", db_path.c_str(), 1);

    const int64_t now = (int64_t)time(NULL);
    const std::string finite_line =
        "0\t1\t0x4029000000000000\t" + std::to_string(now - 120) +
        "\t" + std::to_string(now - 60) + "\t0\tlegacy\tfinite\tUTC\t\t0\t0\n";
    const std::string nan_line =
        "0\t1\t0x7ff8000000000000\t" + std::to_string(now - 120) +
        "\t" + std::to_string(now - 60) + "\t0\tlegacy\tnan\tUTC\t\t0\t0\n";

    int fd = open(db_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0664);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(write(fd, finite_line.data(), finite_line.size()), (ssize_t)finite_line.size());
    ASSERT_EQ(write(fd, nan_line.data(), nan_line.size()), (ssize_t)nan_line.size());
    close(fd);

    ss_sdk_record *rows = NULL;
    size_t count = 0;
    ASSERT_EQ(ss_sdk_db_get_last_weeks(8, &rows, &count), SS_SDK_OK);
    ASSERT_EQ(count, (size_t)1);
    EXPECT_STREQ(rows[0].source_field, "finite");
    EXPECT_TRUE(std::isfinite(rows[0].value.f64));
    ss_sdk_db_free_records(rows);

    remove_file_if_exists(db_path);
    remove_dir_if_exists(dir);
}

TEST(sdk_db_module, read_results_are_deterministically_sorted_by_timeseries_keys)
{
    ScopedEnvVar db_path_guard("SS_SDK_DB_PATH");

    const std::string dir = make_temp_dir();
    ASSERT_FALSE(dir.empty());
    const std::string db_path = dir + "/sdk_db.tsv";
    setenv("SS_SDK_DB_PATH", db_path.c_str(), 1);

    const int64_t now = (int64_t)time(NULL);
    const ss_sdk_record late = make_base_record(2.0, now - 120, now - 10, "temperature_2m_late");
    const ss_sdk_record early = make_base_record(1.0, now - 120, now - 20, "temperature_2m_early");

    ASSERT_EQ(ss_sdk_db_write_record(&late), SS_SDK_OK);
    ASSERT_EQ(ss_sdk_db_write_record(&early), SS_SDK_OK);

    ss_sdk_record *rows = NULL;
    size_t count = 0;
    ASSERT_EQ(ss_sdk_db_get_last_weeks(8, &rows, &count), SS_SDK_OK);
    ASSERT_EQ(count, (size_t)2);
    EXPECT_STREQ(rows[0].source_field, "temperature_2m_early");
    EXPECT_STREQ(rows[1].source_field, "temperature_2m_late");
    EXPECT_LT(rows[0].ts_end_utc, rows[1].ts_end_utc);
    ss_sdk_db_free_records(rows);

    remove_file_if_exists(db_path);
    remove_dir_if_exists(dir);
}
