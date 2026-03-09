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
#include <netinet/in.h>
#include <signal.h>
#include <sqlite3.h>
#include <sys/socket.h>
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

std::string make_system_blob(const char *location_json)
{
    return std::string("{\"location\":") + location_json + "}";
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

int pick_free_tcp_port()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    socklen_t len = sizeof(addr);

    if (fd < 0) {
        return -1;
    }

    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    if (getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
        close(fd);
        return -1;
    }

    close(fd);
    return (int)ntohs(addr.sin_port);
}

pid_t start_http_fixture_server(const std::string &root_dir, int port)
{
    pid_t child = fork();

    if (child < 0) {
        return -1;
    }
    if (child == 0) {
        (void)chdir(root_dir.c_str());
        execlp("python3", "python3", "-m", "http.server", std::to_string(port).c_str(), "--bind", "127.0.0.1", (char *)NULL);
        _exit(127);
    }

    for (int i = 0; i < 50; ++i) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0) {
            sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons((uint16_t)port);
            if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0) {
                close(fd);
                return child;
            }
            close(fd);
        }
        usleep(100000);
    }

    kill(child, SIGTERM);
    (void)waitpid(child, NULL, 0);
    return -1;
}

void stop_http_fixture_server(pid_t pid)
{
    if (pid <= 0) {
        return;
    }
    kill(pid, SIGTERM);
    (void)waitpid(pid, NULL, 0);
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

double expected_fixture_temperature(int64_t ts_utc)
{
    return 2.0 + (double)((ts_utc / 3600) % 24);
}

double expected_fixture_cloud_cover(int64_t ts_utc)
{
    return (double)((ts_utc / 3600) % 100);
}

double expected_fixture_radiation(int64_t ts_utc)
{
    const int hour = (int)((ts_utc / 3600) % 24);
    return (hour >= 6 && hour <= 18) ? (200.0 + (double)(hour * 10)) : 0.0;
}

void expect_exact_f64_sample(ss_metric_id metric, int64_t ts_utc, double expected_value)
{
    ss_sdk_samples_out out = {NULL, 0};

    ASSERT_EQ(ss_sdk_db_get_canonical(ts_utc, 1, metric, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_EQ(out.samples[0].ts_utc, ts_utc);
    EXPECT_DOUBLE_EQ(out.samples[0].value.f64, expected_value);
    EXPECT_EQ(out.samples[0].flags, SS_SDK_SAMPLE_OBSERVED);
    ss_sdk_db_free_samples(&out);
}

void write_exact_observation(ss_metric_id metric, int64_t ts_utc, double value)
{
    ss_sdk_record rec;

    ASSERT_EQ(ss_sdk_record_make_f64(&rec, metric, value, ts_utc, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);
    ASSERT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_OK);
}

int count_exact_rows(const std::string &db_path, ss_metric_id metric, int data_kind, int64_t ts_start_utc)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int count = -1;
    const char *sql =
        "SELECT COUNT(*) FROM records "
        "WHERE canonical = ?1 AND data_kind = ?2 AND ts_start_utc = ?3;";

    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return -1;
    }
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    sqlite3_bind_int(stmt, 1, (int)metric);
    sqlite3_bind_int(stmt, 2, data_kind);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)ts_start_utc);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
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
    {
        http_server_pid_ = -1;
        http_port_ = -1;
    }

    void SetUp() override
    {
        dir_ = make_temp_dir();
        ASSERT_FALSE(dir_.empty());
        db_dir_ = dir_ + "/db";
        ASSERT_EQ(mkdir(db_dir_.c_str(), 0775), 0);
        fixture_json_path_ = dir_ + "/archive.json";
        db_path_ = db_dir_ + "/test-home.db";
    }

    void TearDown() override
    {
        stop_http_fixture_server(http_server_pid_);
        ss_sdk_shutdown();
        remove_file_if_exists(fixture_json_path_);
        remove_dir_if_exists(db_dir_);
        remove_dir_if_exists(dir_);
    }

    std::string dir_;
    std::string db_dir_;
    std::string db_path_;
    std::string fixture_json_path_;
    pid_t http_server_pid_;
    int http_port_;

    ScopedEnvVar cfg_guard_;
    ScopedEnvVar system_guard_;
    ScopedEnvVar db_dir_guard_;
    ScopedEnvVar log_level_guard_;
    ScopedEnvVar mirror_enabled_guard_;
    ScopedEnvVar mirror_path_guard_;

    void set_common_sdk_env()
    {
        ASSERT_EQ(setenv("SS_SDK_DB_DIR", db_dir_.c_str(), 1), 0);
        ASSERT_EQ(setenv("SS_SDK_LOG_LEVEL", "error", 1), 0);
        ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "0", 1), 0);
    }

    void set_system_location(const char *location_json)
    {
        const std::string blob = make_system_blob(location_json);
        ASSERT_EQ(setenv("SUNSPOTS_SYSTEM", blob.c_str(), 1), 0);
    }

    std::string start_http_server()
    {
        http_port_ = pick_free_tcp_port();
        EXPECT_GT(http_port_, 0);
        http_server_pid_ = start_http_fixture_server(dir_, http_port_);
        EXPECT_GT(http_server_pid_, 0);
        return std::string("http://127.0.0.1:") + std::to_string(http_port_);
    }
};

}  // namespace

TEST_F(BackfillWorkerFixture, one_week_backfill_populates_required_metrics_and_is_idempotent)
{
    const int lag_minutes = 120;
    const int64_t end_utc = align_to_slot((int64_t)time(NULL) - (int64_t)lag_minutes * 60);
    const int64_t requested_start_utc = align_to_slot(end_utc - (7LL * 86400LL));
    const int64_t start_date_midnight_utc = requested_start_utc - (requested_start_utc % 86400);
    const int64_t midday_utc = start_date_midnight_utc + (12LL * 3600LL);
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
            "{\"location\":{\"id\":\"test-home\",\"name\":\"Test location\",\"latitude\":59.3293,\"longitude\":18.0686,\"elprisomrade\":\"SE3\"}}",
            1),
        0);
    set_common_sdk_env();

    rc = run_backfill_binary_once();
    ASSERT_EQ(rc, 0);

    ASSERT_GT(count_dir_files(db_dir_), (size_t)0);
    expect_exact_f64_sample(
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        start_date_midnight_utc,
        expected_fixture_temperature(start_date_midnight_utc));
    expect_exact_f64_sample(
        SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT,
        start_date_midnight_utc,
        expected_fixture_cloud_cover(start_date_midnight_utc));
    expect_exact_f64_sample(
        SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2,
        midday_utc,
        expected_fixture_radiation(midday_utc));

    ss_sdk_shutdown();

    rc = run_backfill_binary_once();
    ASSERT_EQ(rc, 0);

    ASSERT_GT(count_dir_files(db_dir_), (size_t)0);
    expect_exact_f64_sample(
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        start_date_midnight_utc,
        expected_fixture_temperature(start_date_midnight_utc));
    expect_exact_f64_sample(
        SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT,
        start_date_midnight_utc,
        expected_fixture_cloud_cover(start_date_midnight_utc));
    expect_exact_f64_sample(
        SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2,
        midday_utc,
        expected_fixture_radiation(midday_utc));
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
    set_common_sdk_env();

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
            "{\"location\":{\"id\":\"test-home\",\"name\":\"Test\",\"longitude\":18.0686,\"elprisomrade\":\"SE3\"}}",
            1),
        0);
    set_common_sdk_env();

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
            "{\"location\":{\"id\":\"test-home\",\"name\":\"Test\",\"latitude\":59.3293,\"elprisomrade\":\"SE3\"}}",
            1),
        0);
    set_common_sdk_env();

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
            "{\"location\":{\"id\":\"test-home\",\"name\":\"Test\",\"latitude\":59.3293,\"longitude\":18.0686,\"elprisomrade\":\"SE3\"}}",
            1),
        0);
    set_common_sdk_env();

    EXPECT_EQ(run_backfill_binary_once(), 0);
    EXPECT_EQ(count_dir_files(db_dir_), (size_t)0);
}

TEST_F(BackfillWorkerFixture, backfill_fails_when_location_id_is_missing)
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
    set_system_location("{\"name\":\"Test\",\"latitude\":59.3293,\"longitude\":18.0686,\"elprisomrade\":\"SE3\"}");
    set_common_sdk_env();

    EXPECT_EQ(run_backfill_binary_once(), 1);
    EXPECT_EQ(count_dir_files(db_dir_), (size_t)0);
}

TEST_F(BackfillWorkerFixture, backfill_fails_when_module_config_is_missing)
{
    ASSERT_EQ(unsetenv("SUNSPOTS_CONFIG"), 0);
    set_system_location("{\"id\":\"test-home\",\"name\":\"Test\",\"latitude\":59.3293,\"longitude\":18.0686,\"elprisomrade\":\"SE3\"}");
    set_common_sdk_env();

    EXPECT_EQ(run_backfill_binary_once(), 1);
    EXPECT_EQ(count_dir_files(db_dir_), (size_t)0);
}

TEST_F(BackfillWorkerFixture, multi_chunk_backfill_writes_across_chunk_boundaries)
{
    const int lag_minutes = 120;
    const int64_t end_utc = align_to_slot((int64_t)time(NULL) - (int64_t)lag_minutes * 60);
    const int64_t requested_start_utc = align_to_slot(end_utc - (3LL * 86400LL));
    const int64_t start_date_midnight_utc = requested_start_utc - (requested_start_utc % 86400);
    const int64_t second_day_midday_utc = start_date_midnight_utc + 86400LL + (12LL * 3600LL);
    const int64_t fixture_from = start_date_midnight_utc - 3600;
    const int64_t fixture_to = end_utc + 3600;
    const std::string start_date = format_utc_ymd(requested_start_utc);
    std::string cfg;

    ASSERT_TRUE(write_mock_archive_json(fixture_json_path_, fixture_from, fixture_to));
    ASSERT_FALSE(start_date.empty());

    cfg =
        "{"
        "\"name\":\"BackfillOpenMeteo\","
        "\"backfill\":{"
        "\"enabled\":true,"
        "\"start_date_utc\":\"" + start_date + "\","
        "\"chunk_days\":1,"
        "\"retry_max_attempts\":2,"
        "\"retry_base_backoff_ms\":50,"
        "\"freshness_lag_minutes\":120,"
        "\"request_interval_ms\":100,"
        "\"max_requests_per_minute\":60,"
        "\"max_requests_per_hour\":500,"
        "\"max_requests_per_day\":2000,"
        "\"endpoint\":\"file://" + fixture_json_path_ + "\""
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg.c_str(), 1), 0);
    set_system_location("{\"id\":\"test-home\",\"name\":\"Test location\",\"latitude\":59.3293,\"longitude\":18.0686,\"elprisomrade\":\"SE3\"}");
    set_common_sdk_env();

    ASSERT_EQ(run_backfill_binary_once(), 0);
    expect_exact_f64_sample(
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        second_day_midday_utc,
        expected_fixture_temperature(second_day_midday_utc));
    expect_exact_f64_sample(
        SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2,
        second_day_midday_utc,
        expected_fixture_radiation(second_day_midday_utc));
}

TEST_F(BackfillWorkerFixture, backfill_fills_missing_metrics_without_overwriting_existing_observation)
{
    const int lag_minutes = 120;
    const int64_t end_utc = align_to_slot((int64_t)time(NULL) - (int64_t)lag_minutes * 60);
    const int64_t requested_start_utc = align_to_slot(end_utc - 86400LL);
    const int64_t start_date_midnight_utc = requested_start_utc - (requested_start_utc % 86400);
    const int64_t fixture_from = start_date_midnight_utc - 3600;
    const int64_t fixture_to = end_utc + 3600;
    const std::string start_date = format_utc_ymd(requested_start_utc);
    std::string cfg;

    ASSERT_TRUE(write_mock_archive_json(fixture_json_path_, fixture_from, fixture_to));
    ASSERT_FALSE(start_date.empty());

    cfg =
        "{"
        "\"name\":\"BackfillOpenMeteo\","
        "\"backfill\":{"
        "\"enabled\":true,"
        "\"start_date_utc\":\"" + start_date + "\","
        "\"chunk_days\":1,"
        "\"retry_max_attempts\":2,"
        "\"retry_base_backoff_ms\":50,"
        "\"freshness_lag_minutes\":120,"
        "\"request_interval_ms\":100,"
        "\"max_requests_per_minute\":60,"
        "\"max_requests_per_hour\":500,"
        "\"max_requests_per_day\":2000,"
        "\"endpoint\":\"file://" + fixture_json_path_ + "\""
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg.c_str(), 1), 0);
    set_system_location("{\"id\":\"test-home\",\"name\":\"Test location\",\"latitude\":59.3293,\"longitude\":18.0686,\"elprisomrade\":\"SE3\"}");
    set_common_sdk_env();

    write_exact_observation(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, start_date_midnight_utc, 99.0);
    ss_sdk_shutdown();

    ASSERT_EQ(run_backfill_binary_once(), 0);
    expect_exact_f64_sample(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, start_date_midnight_utc, 99.0);
    expect_exact_f64_sample(
        SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT,
        start_date_midnight_utc,
        expected_fixture_cloud_cover(start_date_midnight_utc));
    expect_exact_f64_sample(
        SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2,
        start_date_midnight_utc,
        expected_fixture_radiation(start_date_midnight_utc));
}

TEST_F(BackfillWorkerFixture, forecast_history_writes_forecast_rows_and_rerun_deduplicates_exact_releases)
{
    const int lag_minutes = 120;
    const int64_t now_slot = align_to_slot((int64_t)time(NULL));
    const int64_t end_utc = align_to_slot((int64_t)time(NULL) - (int64_t)lag_minutes * 60);
    const int64_t requested_start_utc = align_to_slot(end_utc - (3LL * 3600LL));
    const int64_t start_date_midnight_utc = requested_start_utc - (requested_start_utc % 86400);
    const int64_t fixture_from = start_date_midnight_utc - 3600;
    const int64_t fixture_to = now_slot + (6LL * 3600LL);
    const int64_t future_slot = ((now_slot / 3600) + 2LL) * 3600LL;
    const std::string start_date = format_utc_ymd(requested_start_utc);
    const std::string server_base = start_http_server();
    std::string cfg;
    ss_sdk_samples_out out = {NULL, 0};
    int first_count = 0;
    int second_count = 0;

    ASSERT_TRUE(write_mock_archive_json(fixture_json_path_, fixture_from, fixture_to));
    ASSERT_FALSE(start_date.empty());

    cfg =
        "{"
        "\"name\":\"BackfillOpenMeteo\","
        "\"backfill\":{"
        "\"enabled\":true,"
        "\"start_date_utc\":\"" + start_date + "\","
        "\"chunk_days\":1,"
        "\"retry_max_attempts\":2,"
        "\"retry_base_backoff_ms\":50,"
        "\"freshness_lag_minutes\":120,"
        "\"request_interval_ms\":50,"
        "\"max_requests_per_minute\":600,"
        "\"max_requests_per_hour\":5000,"
        "\"max_requests_per_day\":10000,"
        "\"endpoint\":\"" + server_base + "/archive.json\","
        "\"single_runs_endpoint\":\"" + server_base + "/archive.json\""
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg.c_str(), 1), 0);
    set_system_location("{\"id\":\"test-home\",\"name\":\"Test location\",\"latitude\":59.3293,\"longitude\":18.0686,\"elprisomrade\":\"SE3\"}");
    set_common_sdk_env();

    ASSERT_EQ(run_backfill_binary_once(), 0);
    ASSERT_EQ(ss_sdk_db_get_canonical(future_slot, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_EQ(out.samples[0].ts_utc, future_slot);
    EXPECT_DOUBLE_EQ(out.samples[0].value.f64, expected_fixture_temperature(future_slot));
    EXPECT_EQ(out.samples[0].flags, SS_SDK_SAMPLE_FORECAST);
    ss_sdk_db_free_samples(&out);

    first_count = count_exact_rows(db_path_, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, SS_SDK_DATA_FORECAST, future_slot);
    ASSERT_GT(first_count, 0);

    ss_sdk_shutdown();
    ASSERT_EQ(run_backfill_binary_once(), 0);

    second_count = count_exact_rows(db_path_, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, SS_SDK_DATA_FORECAST, future_slot);
    ASSERT_EQ(second_count, first_count);
}
