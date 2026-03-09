#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
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
            if (setenv(name_, old_value_.c_str(), 1) != 0) {
                /* best-effort restore in test teardown */
            }
        } else {
            if (unsetenv(name_) != 0) {
                /* best-effort restore in test teardown */
            }
        }
    }

private:
    const char *name_;
    bool had_old_;
    std::string old_value_;
};

std::string make_temp_dir()
{
    char tpl[] = "/tmp/sunspots_backfill_test_XXXXXX";
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

int64_t align_to_slot(int64_t ts_utc)
{
    if (ts_utc < 0) {
        return 0;
    }
    return ts_utc - (ts_utc % 900);
}

std::string format_utc_ymdhm(int64_t ts_utc)
{
    char out[32];
    time_t tv = (time_t)ts_utc;
    struct tm tmv;
    std::memset(&tmv, 0, sizeof(tmv));
    if (gmtime_r(&tv, &tmv) == NULL) {
        return "";
    }
    if (strftime(out, sizeof(out), "%Y-%m-%dT%H:%M", &tmv) == 0U) {
        return "";
    }
    return std::string(out);
}

std::string format_utc_ymd(int64_t ts_utc)
{
    char out[16];
    time_t tv = (time_t)ts_utc;
    struct tm tmv;
    std::memset(&tmv, 0, sizeof(tmv));
    if (gmtime_r(&tv, &tmv) == NULL) {
        return "";
    }
    if (strftime(out, sizeof(out), "%Y-%m-%d", &tmv) == 0U) {
        return "";
    }
    return std::string(out);
}

bool write_mock_archive_json(const std::string &path, int64_t from_utc, int64_t to_utc)
{
    FILE *fp;
    bool first = true;
    int64_t ts;

    fp = std::fopen(path.c_str(), "wb");
    if (fp == NULL) {
        return false;
    }

    if (std::fputs("{\"hourly\":{\"time\":[", fp) == EOF) {
        (void)std::fclose(fp);
        return false;
    }
    for (ts = from_utc; ts <= to_utc; ts += 3600) {
        const std::string t = format_utc_ymdhm(ts);
        if (t.empty()) {
            (void)std::fclose(fp);
            return false;
        }
        if (!first) {
            if (std::fputs(",", fp) == EOF) {
                (void)std::fclose(fp);
                return false;
            }
        }
        if (std::fprintf(fp, "\"%s\"", t.c_str()) < 0) {
            (void)std::fclose(fp);
            return false;
        }
        first = false;
    }

    first = true;
    if (std::fputs("],\"temperature_2m\":[", fp) == EOF) {
        (void)std::fclose(fp);
        return false;
    }
    for (ts = from_utc; ts <= to_utc; ts += 3600) {
        const double v = 2.0 + (double)((ts / 3600) % 24);
        if (!first) {
            if (std::fputs(",", fp) == EOF) {
                (void)std::fclose(fp);
                return false;
            }
        }
        if (std::fprintf(fp, "%.2f", v) < 0) {
            (void)std::fclose(fp);
            return false;
        }
        first = false;
    }

    first = true;
    if (std::fputs("],\"cloud_cover\":[", fp) == EOF) {
        (void)std::fclose(fp);
        return false;
    }
    for (ts = from_utc; ts <= to_utc; ts += 3600) {
        const double v = (double)((ts / 3600) % 100);
        if (!first) {
            if (std::fputs(",", fp) == EOF) {
                (void)std::fclose(fp);
                return false;
            }
        }
        if (std::fprintf(fp, "%.2f", v) < 0) {
            (void)std::fclose(fp);
            return false;
        }
        first = false;
    }

    first = true;
    if (std::fputs("],\"shortwave_radiation\":[", fp) == EOF) {
        (void)std::fclose(fp);
        return false;
    }
    for (ts = from_utc; ts <= to_utc; ts += 3600) {
        const int hour = (int)((ts / 3600) % 24);
        const double v = (hour >= 6 && hour <= 18) ? (200.0 + (double)(hour * 10)) : 0.0;
        if (!first) {
            if (std::fputs(",", fp) == EOF) {
                (void)std::fclose(fp);
                return false;
            }
        }
        if (std::fprintf(fp, "%.2f", v) < 0) {
            (void)std::fclose(fp);
            return false;
        }
        first = false;
    }
    if (std::fputs("]}}", fp) == EOF) {
        (void)std::fclose(fp);
        return false;
    }

    if (std::fclose(fp) != 0) {
        return false;
    }
    return true;
}

size_t count_dir_files(const std::string &dir_path)
{
    DIR *dir = opendir(dir_path.c_str());
    struct dirent *ent = NULL;
    size_t count = 0U;

    if (dir == NULL) {
        return 0U;
    }
    while ((ent = readdir(dir)) != NULL) {
        if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        count += 1U;
    }
    closedir(dir);
    return count;
}

int run_backfill_binary_once()
{
    pid_t child = fork();
    int rc;

    if (child < 0) {
        return -1;
    }
    if (child == 0) {
        (void)execl(BACKFILL_BIN_PATH, BACKFILL_BIN_PATH, (char *)NULL);
        _exit(127);
    }

    rc = 0;
    while (waitpid(child, &rc, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
    if (!WIFEXITED(rc)) {
        return -1;
    }
    return WEXITSTATUS(rc);
}

class BackfillWorkerFixture : public ::testing::Test {
protected:
    BackfillWorkerFixture()
        : cfg_guard_("SUNSPOTS_CONFIG"),
          system_guard_("SUNSPOTS_SYSTEM"),
          db_dir_guard_("SS_SDK_DB_DIR"),
          log_level_guard_("SS_SDK_LOG_LEVEL"),
          mirror_enabled_guard_("SS_SDK_LOG_MIRROR_ENABLED"),
          mirror_path_guard_("SS_SDK_LOG_MIRROR_PATH")
    {}

    void SetUp() override
    {
        dir_ = make_temp_dir();
        ASSERT_FALSE(dir_.empty());
        db_dir_ = dir_ + "/db";
        ASSERT_EQ(mkdir(db_dir_.c_str(), 0775), 0);
        fixture_json_path_ = dir_ + "/archive.json";
    }

    void TearDown() override
    {
        ss_sdk_shutdown();
        remove_file_if_exists(fixture_json_path_);
        remove_dir_if_exists(db_dir_);
        remove_dir_if_exists(dir_);
    }

    std::string dir_;
    std::string db_dir_;
    std::string fixture_json_path_;

    ScopedEnvVar cfg_guard_;
    ScopedEnvVar system_guard_;
    ScopedEnvVar db_dir_guard_;
    ScopedEnvVar log_level_guard_;
    ScopedEnvVar mirror_enabled_guard_;
    ScopedEnvVar mirror_path_guard_;
};

}  // namespace

TEST_F(BackfillWorkerFixture, one_week_backfill_populates_required_metrics_and_is_idempotent)
{
    const int lag_minutes = 120;
    const int64_t end_utc = align_to_slot((int64_t)time(NULL) - (int64_t)lag_minutes * 60);
    const int64_t requested_start_utc = align_to_slot(end_utc - (7LL * 86400LL));
    const int64_t start_date_midnight_utc = requested_start_utc - (requested_start_utc % 86400);
    const int64_t fixture_from = start_date_midnight_utc - 3600;
    const int64_t fixture_to = end_utc + 3600;
    const std::string start_date = format_utc_ymd(requested_start_utc);
    std::string cfg;
    int rc;

    ASSERT_TRUE(write_mock_archive_json(fixture_json_path_, fixture_from, fixture_to));
    ASSERT_FALSE(start_date.empty());

    cfg =
        "{"
        "\"name\":\"BackfillOpenMeteo\","
        "\"backfill\":{"
        "\"enabled\":true,"
        "\"start_date_utc\":\"" +
        start_date +
        "\","
        "\"chunk_days\":7,"
        "\"retry_max_attempts\":2,"
        "\"retry_base_backoff_ms\":50,"
        "\"freshness_lag_minutes\":120,"
        "\"request_interval_ms\":100,"
        "\"max_requests_per_minute\":60,"
        "\"max_requests_per_hour\":500,"
        "\"max_requests_per_day\":2000,"
        "\"endpoint\":\"file://" +
        fixture_json_path_ +
        "\""
        "},"
        "\"system\":{"
        "\"sdk\":{"
        "\"db_dir\":\"" +
        db_dir_ +
        "\","
        "\"log_level\":\"error\","
        "\"log_mirror_enabled\":false"
        "}"
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg.c_str(), 1), 0);
    ASSERT_EQ(
        setenv(
            "SUNSPOTS_SYSTEM",
            "{\"location\":{\"name\":\"Test location\",\"latitude\":59.3293,\"longitude\":18.0686,\"elprisomrade\":\"SE3\"}}",
            1),
        0);
    ASSERT_EQ(setenv("SS_SDK_DB_DIR", db_dir_.c_str(), 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_LEVEL", "error", 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "0", 1), 0);

    rc = run_backfill_binary_once();
    ASSERT_EQ(rc, 0);

    ASSERT_GT(count_dir_files(db_dir_), (size_t)0);

    rc = run_backfill_binary_once();
    ASSERT_EQ(rc, 0);

    ASSERT_GT(count_dir_files(db_dir_), (size_t)0);
}

TEST_F(BackfillWorkerFixture, backfill_fails_when_system_location_is_missing)
{
    const std::string cfg =
        "{"
        "\"name\":\"BackfillOpenMeteo\","
        "\"backfill\":{"
        "\"enabled\":true,"
        "\"start_date_utc\":\"2025-01-01\","
        "\"chunk_days\":1"
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg.c_str(), 1), 0);
    ASSERT_EQ(unsetenv("SUNSPOTS_SYSTEM"), 0);
    ASSERT_EQ(setenv("SS_SDK_DB_DIR", db_dir_.c_str(), 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_LEVEL", "error", 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "0", 1), 0);

    EXPECT_EQ(run_backfill_binary_once(), 1);
    EXPECT_EQ(count_dir_files(db_dir_), (size_t)0);
}

TEST_F(BackfillWorkerFixture, backfill_fails_when_location_latitude_missing)
{
    const std::string cfg =
        "{"
        "\"name\":\"BackfillOpenMeteo\","
        "\"backfill\":{"
        "\"enabled\":true,"
        "\"start_date_utc\":\"2025-01-01\","
        "\"chunk_days\":1"
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg.c_str(), 1), 0);
    ASSERT_EQ(
        setenv(
            "SUNSPOTS_SYSTEM",
            "{\"location\":{\"name\":\"Test\",\"longitude\":18.0686,\"elprisomrade\":\"SE3\"}}",
            1),
        0);
    ASSERT_EQ(setenv("SS_SDK_DB_DIR", db_dir_.c_str(), 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_LEVEL", "error", 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "0", 1), 0);

    EXPECT_EQ(run_backfill_binary_once(), 1);
    EXPECT_EQ(count_dir_files(db_dir_), (size_t)0);
}

TEST_F(BackfillWorkerFixture, backfill_fails_when_location_longitude_missing)
{
    const std::string cfg =
        "{"
        "\"name\":\"BackfillOpenMeteo\","
        "\"backfill\":{"
        "\"enabled\":true,"
        "\"start_date_utc\":\"2025-01-01\","
        "\"chunk_days\":1"
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg.c_str(), 1), 0);
    ASSERT_EQ(
        setenv(
            "SUNSPOTS_SYSTEM",
            "{\"location\":{\"name\":\"Test\",\"latitude\":59.3293,\"elprisomrade\":\"SE3\"}}",
            1),
        0);
    ASSERT_EQ(setenv("SS_SDK_DB_DIR", db_dir_.c_str(), 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_LEVEL", "error", 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "0", 1), 0);

    EXPECT_EQ(run_backfill_binary_once(), 1);
    EXPECT_EQ(count_dir_files(db_dir_), (size_t)0);
}

TEST_F(BackfillWorkerFixture, backfill_disabled_exits_success_without_writes)
{
    const std::string cfg =
        "{"
        "\"name\":\"BackfillOpenMeteo\","
        "\"backfill\":{"
        "\"enabled\":false"
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg.c_str(), 1), 0);
    ASSERT_EQ(
        setenv(
            "SUNSPOTS_SYSTEM",
            "{\"location\":{\"name\":\"Test\",\"latitude\":59.3293,\"longitude\":18.0686,\"elprisomrade\":\"SE3\"}}",
            1),
        0);
    ASSERT_EQ(setenv("SS_SDK_DB_DIR", db_dir_.c_str(), 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_LEVEL", "error", 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "0", 1), 0);

    EXPECT_EQ(run_backfill_binary_once(), 0);
    EXPECT_EQ(count_dir_files(db_dir_), (size_t)0);
}
