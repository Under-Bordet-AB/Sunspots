#include <gtest/gtest.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>

#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "sdk/ss_sdk.h"
#include "sdk/internal/db/ss_db_internal.h"
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

std::string read_text_file(const std::string &path)
{
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (fp == NULL) {
        return "";
    }

    std::string out;
    char buf[512];
    size_t nr;
    while ((nr = std::fread(buf, 1, sizeof(buf), fp)) > 0) {
        out.append(buf, nr);
    }
    if (std::fclose(fp) != 0) {
        return "";
    }
    return out;
}

class ScopedCwd {
public:
    explicit ScopedCwd(const std::string &next) : ok_(false)
    {
        char buf[PATH_MAX];
        if (getcwd(buf, sizeof(buf)) == NULL) {
            return;
        }
        old_ = buf;
        if (chdir(next.c_str()) != 0) {
            return;
        }
        ok_ = true;
    }

    ~ScopedCwd()
    {
        if (!old_.empty()) {
            if (chdir(old_.c_str()) != 0) {
            }
        }
    }

    bool ok() const { return ok_; }

private:
    bool ok_;
    std::string old_;
};

void remove_file_if_exists(const std::string &path)
{
    (void)unlink(path.c_str());
}

void remove_dir_if_exists(const std::string &path)
{
    (void)rmdir(path.c_str());
}

int64_t now_slot_utc()
{
    const int64_t now_utc = (int64_t)time(NULL);
    if (now_utc < 0) {
        return 0;
    }
    return now_utc - (now_utc % 900);
}

ss_sdk_record make_f64_record(ss_metric_id metric, double value, int64_t ts_start_utc, ss_sdk_data_kind data_kind)
{
    ss_sdk_record rec;
    std::memset(&rec, 0, sizeof(rec));
    rec.metric = metric;
    rec.value_type = SS_SDK_VALUE_F64;
    rec.value.f64 = value;
    rec.ts_start_utc = ts_start_utc;
    rec.ts_end_utc = ts_start_utc + 900;
    rec.data_kind = data_kind;
    return rec;
}

ss_sdk_record make_i64_record(ss_metric_id metric, int64_t value, int64_t ts_start_utc, ss_sdk_data_kind data_kind)
{
    ss_sdk_record rec;
    std::memset(&rec, 0, sizeof(rec));
    rec.metric = metric;
    rec.value_type = SS_SDK_VALUE_I64;
    rec.value.i64 = value;
    rec.ts_start_utc = ts_start_utc;
    rec.ts_end_utc = ts_start_utc + 900;
    rec.data_kind = data_kind;
    return rec;
}

ss_sdk_status write_f64_record(ss_metric_id metric, double value, int64_t ts_start_utc, ss_sdk_data_kind data_kind)
{
    const ss_sdk_record rec = make_f64_record(metric, value, ts_start_utc, data_kind);
    return ss_sdk_db_write_record(&rec);
}

ss_sdk_status write_i64_record(ss_metric_id metric, int64_t value, int64_t ts_start_utc, ss_sdk_data_kind data_kind)
{
    const ss_sdk_record rec = make_i64_record(metric, value, ts_start_utc, data_kind);
    return ss_sdk_db_write_record(&rec);
}

class SdkDbFixture : public ::testing::Test {
protected:
    SdkDbFixture()
        : db_dir_guard_("SS_SDK_DB_DIR"),
          system_guard_("SUNSPOTS_SYSTEM"),
          sdk_log_level_guard_("SS_SDK_LOG_LEVEL"),
          sdk_log_mirror_enabled_guard_("SS_SDK_LOG_MIRROR_ENABLED"),
          sdk_log_mirror_path_guard_("SS_SDK_LOG_MIRROR_PATH")
    {}

    void SetUp() override
    {
        dir_ = make_temp_dir();
        ASSERT_FALSE(dir_.empty());
        db_dir_ = dir_ + "/db";
        ASSERT_EQ(mkdir(db_dir_.c_str(), 0775), 0);
        db_path_ = db_dir_ + "/59329300_18068600.db";
        ASSERT_EQ(setenv("SS_SDK_DB_DIR", db_dir_.c_str(), 1), 0);
        ASSERT_EQ(
            setenv(
                "SUNSPOTS_SYSTEM",
                "{\"location\":{\"name\":\"Test location\",\"latitude\":59.3293,\"longitude\":18.0686,\"elprisomrade\":\"SE3\"}}",
                1),
            0);
    }

    void TearDown() override
    {
        ss_sdk_shutdown();
        remove_file_if_exists(db_path_);
        remove_dir_if_exists(db_dir_);
        remove_dir_if_exists(dir_);
    }

    // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
    ScopedEnvVar db_dir_guard_;
    ScopedEnvVar system_guard_;
    ScopedEnvVar sdk_log_level_guard_;
    ScopedEnvVar sdk_log_mirror_enabled_guard_;
    ScopedEnvVar sdk_log_mirror_path_guard_;
    std::string dir_;
    std::string db_dir_;
    std::string db_path_;
    // NOLINTEND(misc-non-private-member-variables-in-classes)
};

}  // namespace

TEST_F(SdkDbFixture, write_record_rejects_null)
{
    EXPECT_EQ(ss_sdk_db_write_record(NULL), SS_SDK_ERR_INVALID_ARG);
}

TEST_F(SdkDbFixture, batch_write_is_atomic_on_validation_failure)
{
    const int64_t slot = now_slot_utc() - 1800;
    ss_sdk_record records[2];
    ss_sdk_samples_out out = {NULL, 0};
    size_t written = 0U;

    records[0] = make_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 12.0, slot, SS_SDK_DATA_OBSERVATION);
    records[1] = make_f64_record(SS_METRIC_WEATHER_IS_DAY, 1.0, slot, SS_SDK_DATA_OBSERVATION);

    EXPECT_EQ(ss_sdk_db_write_records(records, 2U, &written), SS_SDK_ERR_VALIDATION);
    EXPECT_EQ(written, (size_t)0);

    EXPECT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_ERR_PARTIAL_DATA);
    EXPECT_EQ(out.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out.count, (size_t)0);
}

TEST_F(SdkDbFixture, batch_write_commits_all_rows_in_one_call)
{
    const int64_t slot = now_slot_utc() - 1800;
    ss_sdk_record records[2];
    ss_sdk_samples_out out_temp = {NULL, 0};
    ss_sdk_samples_out out_cloud = {NULL, 0};
    size_t written = 0U;

    records[0] = make_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 8.5, slot, SS_SDK_DATA_OBSERVATION);
    records[1] = make_f64_record(SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT, 77.0, slot, SS_SDK_DATA_OBSERVATION);

    ASSERT_EQ(ss_sdk_db_write_records(records, 2U, &written), SS_SDK_OK);
    EXPECT_EQ(written, (size_t)2);

    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out_temp), SS_SDK_OK);
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT, &out_cloud), SS_SDK_OK);
    ASSERT_EQ(out_temp.count, (size_t)1);
    ASSERT_EQ(out_cloud.count, (size_t)1);
    ss_sdk_db_free_samples(&out_temp);
    ss_sdk_db_free_samples(&out_cloud);
}

TEST_F(SdkDbFixture, write_record_rejects_unknown_metric)
{
    ss_sdk_record rec = make_f64_record((ss_metric_id)SS_METRIC_COUNT, 1.0, now_slot_utc(), SS_SDK_DATA_OBSERVATION);
    EXPECT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_ERR_VALIDATION);
}

TEST_F(SdkDbFixture, write_record_rejects_value_type_mismatch_with_metric)
{
    ss_sdk_record rec = make_i64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 1, now_slot_utc(), SS_SDK_DATA_OBSERVATION);
    EXPECT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_ERR_VALIDATION);
}

TEST_F(SdkDbFixture, write_record_rejects_non_finite_f64)
{
    ss_sdk_record rec = make_f64_record(
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        std::numeric_limits<double>::quiet_NaN(),
        now_slot_utc(),
        SS_SDK_DATA_OBSERVATION);
    EXPECT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_ERR_VALIDATION);

    rec.value.f64 = std::numeric_limits<double>::infinity();
    EXPECT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_ERR_VALIDATION);
}

TEST_F(SdkDbFixture, write_record_rejects_invalid_data_kind)
{
    ss_sdk_record rec = make_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 2.0, now_slot_utc(), SS_SDK_DATA_OBSERVATION);
    rec.data_kind = (ss_sdk_data_kind)99;
    EXPECT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_ERR_VALIDATION);
}

TEST_F(SdkDbFixture, write_record_rejects_misaligned_start)
{
    const int64_t slot = now_slot_utc();
    ss_sdk_record rec = make_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 2.0, slot + 1, SS_SDK_DATA_OBSERVATION);
    ss_sdk_samples_out out = {NULL, 0};

    ASSERT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_OK);
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_DOUBLE_EQ(out.samples[0].value.f64, 2.0);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, write_record_rejects_non_15m_end)
{
    const int64_t slot = now_slot_utc();
    ss_sdk_record rec = make_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 2.0, slot, SS_SDK_DATA_OBSERVATION);
    rec.ts_end_utc = slot + 1800;
    ss_sdk_samples_out out = {NULL, 0};

    ASSERT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_OK);
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_DOUBLE_EQ(out.samples[0].value.f64, 2.0);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, write_record_accepts_i64_metric_and_reads_back)
{
    const int64_t slot = now_slot_utc();
    const ss_sdk_record rec = make_i64_record(
        SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE,
        3,
        slot,
        SS_SDK_DATA_OBSERVATION);

    ASSERT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_EQ(out.samples[0].value_type, SS_SDK_VALUE_I64);
    EXPECT_EQ(out.samples[0].value.i64, 3);
    EXPECT_EQ(out.samples[0].flags, SS_SDK_SAMPLE_OBSERVED);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, record_factory_f64_builds_normalized_record)
{
    const int64_t base = now_slot_utc();
    ss_sdk_record rec;
    ASSERT_EQ(
        ss_sdk_record_make_f64(
            &rec,
            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
            7.5,
            base + 733,
            SS_SDK_DATA_OBSERVATION),
        SS_SDK_OK);
    EXPECT_EQ(rec.metric, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C);
    EXPECT_EQ(rec.value_type, SS_SDK_VALUE_F64);
    EXPECT_EQ(rec.ts_start_utc, base);
    EXPECT_EQ(rec.ts_end_utc, base + 900);
    EXPECT_EQ(rec.data_kind, SS_SDK_DATA_OBSERVATION);
    EXPECT_DOUBLE_EQ(rec.value.f64, 7.5);
}

TEST_F(SdkDbFixture, record_factory_rejects_metric_type_mismatch)
{
    ss_sdk_record rec;
    EXPECT_EQ(
        ss_sdk_record_make_i64(
            &rec,
            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
            1,
            now_slot_utc(),
            SS_SDK_DATA_OBSERVATION),
        SS_SDK_ERR_VALIDATION);

    EXPECT_EQ(
        ss_sdk_record_make_bool(
            &rec,
            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
            true,
            now_slot_utc(),
            SS_SDK_DATA_OBSERVATION),
        SS_SDK_ERR_VALIDATION);
}

TEST_F(SdkDbFixture, record_factory_output_writes_successfully)
{
    const int64_t base = now_slot_utc();
    ss_sdk_record rec;
    ss_sdk_samples_out out = {NULL, 0};

    ASSERT_EQ(
        ss_sdk_record_make_bool(
            &rec,
            SS_METRIC_WEATHER_IS_DAY,
            true,
            base + 61,
            SS_SDK_DATA_OBSERVATION),
        SS_SDK_OK);
    ASSERT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_OK);
    ASSERT_EQ(ss_sdk_db_get_canonical(base, 1, SS_METRIC_WEATHER_IS_DAY, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_EQ(out.samples[0].value_type, SS_SDK_VALUE_BOOL);
    EXPECT_TRUE(out.samples[0].value.boolean);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, get_canonical_rejects_null_out)
{
    EXPECT_EQ(
        ss_sdk_db_get_canonical(0, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, NULL),
        SS_SDK_ERR_INVALID_ARG);
}

TEST_F(SdkDbFixture, get_canonical_rejects_invalid_canonical)
{
    ss_sdk_samples_out out = {NULL, 0};
    EXPECT_EQ(
        ss_sdk_db_get_canonical(0, 1, (ss_metric_id)SS_METRIC_COUNT, &out),
        SS_SDK_ERR_INVALID_ARG);
    EXPECT_EQ(out.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out.count, (size_t)0);
}

TEST_F(SdkDbFixture, get_canonical_rejects_negative_from_utc)
{
    ss_sdk_samples_out out = {NULL, 0};
    EXPECT_EQ(
        ss_sdk_db_get_canonical(-1, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out),
        SS_SDK_ERR_INVALID_ARG);
}

TEST_F(SdkDbFixture, get_canonical_rejects_quarters_above_cap)
{
    ss_sdk_samples_out out = {NULL, 0};
    EXPECT_EQ(
        ss_sdk_db_get_canonical(0, 673, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out),
        SS_SDK_ERR_INVALID_ARG);
}

TEST_F(SdkDbFixture, get_canonical_accepts_max_quarters)
{
    ss_sdk_samples_out out = {NULL, 0};
    EXPECT_EQ(
        ss_sdk_db_get_canonical(0, 672, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out),
        SS_SDK_ERR_PARTIAL_DATA);
    EXPECT_EQ(out.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out.count, (size_t)0);
}

TEST_F(SdkDbFixture, get_canonical_rejects_range_overflow)
{
    ss_sdk_samples_out out = {NULL, 0};
    EXPECT_EQ(
        ss_sdk_db_get_canonical(std::numeric_limits<int64_t>::max(), 2, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out),
        SS_SDK_ERR_INVALID_ARG);
}

TEST_F(SdkDbFixture, get_canonical_quarters_0_fetches_forward_horizon_from_start_slot)
{
    const int64_t slot0 = now_slot_utc() - 1800;
    const int64_t slot1 = slot0 + 900;
    const int64_t slot2 = slot0 + 1800;
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_WIND_SPEED_10M_MS, 7.5, slot0, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_WIND_SPEED_10M_MS, 8.5, slot1, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_WIND_SPEED_10M_MS, 9.5, slot2, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_samples_out out0 = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(slot0, 0, SS_METRIC_WEATHER_WIND_SPEED_10M_MS, &out0), SS_SDK_OK);
    ASSERT_EQ(out0.count, (size_t)3);
    EXPECT_EQ(out0.samples[0].ts_utc, slot0);
    EXPECT_EQ(out0.samples[1].ts_utc, slot1);
    EXPECT_EQ(out0.samples[2].ts_utc, slot2);
    ss_sdk_db_free_samples(&out0);

    ss_sdk_samples_out out1 = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(slot0, 1, SS_METRIC_WEATHER_WIND_SPEED_10M_MS, &out1), SS_SDK_OK);
    ASSERT_EQ(out1.count, (size_t)1);
    EXPECT_EQ(out1.samples[0].ts_utc, slot0);
    ss_sdk_db_free_samples(&out1);
}

TEST_F(SdkDbFixture, get_canonical_from_zero_fetches_current_slot)
{
    const int64_t slot = now_slot_utc();
    const ss_sdk_record rec = make_f64_record(
        SS_METRIC_WEATHER_WIND_GUST_10M_MS,
        11.0,
        slot,
        SS_SDK_DATA_OBSERVATION);

    ASSERT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(0, 1, SS_METRIC_WEATHER_WIND_GUST_10M_MS, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_EQ(out.samples[0].ts_utc, slot);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, get_canonical_nonzero_from_is_floor_aligned)
{
    const int64_t base = now_slot_utc() - 1800;
    const ss_sdk_record rec = make_f64_record(
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        4.0,
        base,
        SS_SDK_DATA_OBSERVATION);

    ASSERT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(
        ss_sdk_db_get_canonical(base + 733, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out),
        SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_EQ(out.samples[0].ts_utc, base);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, get_canonical_from_1_to_899_aligns_to_unix_epoch_zero)
{
    const ss_sdk_record rec = make_i64_record(
        SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE,
        42,
        0,
        SS_SDK_DATA_OBSERVATION);

    ASSERT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(
        ss_sdk_db_get_canonical(1, 1, SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE, &out),
        SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_EQ(out.samples[0].ts_utc, (int64_t)0);
    EXPECT_EQ(out.samples[0].value.i64, (int64_t)42);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, same_slot_observation_keeps_first_insert_for_canonical_read)
{
    const int64_t slot = now_slot_utc() - 900;
    const ss_sdk_record first = make_f64_record(
        SS_METRIC_WEATHER_PRESSURE_MSL_HPA,
        1000.0,
        slot,
        SS_SDK_DATA_OBSERVATION);
    const ss_sdk_record second = make_f64_record(
        SS_METRIC_WEATHER_PRESSURE_MSL_HPA,
        2000.0,
        slot,
        SS_SDK_DATA_OBSERVATION);

    ASSERT_EQ(ss_sdk_db_write_record(&first), SS_SDK_OK);
    ASSERT_EQ(ss_sdk_db_write_record(&second), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_PRESSURE_MSL_HPA, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_DOUBLE_EQ(out.samples[0].value.f64, 1000.0);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, forecast_history_keeps_multiple_versions_for_same_slot)
{
    const int64_t slot = now_slot_utc() + 3600;
    const ss_sdk_record first = make_f64_record(
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        1.0,
        slot,
        SS_SDK_DATA_FORECAST);
    const ss_sdk_record second = make_f64_record(
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        3.0,
        slot,
        SS_SDK_DATA_FORECAST);
    ss_sdk_record first_mut = first;
    ss_sdk_record second_mut = second;
    ss_sdk_samples_out out = {NULL, 0};

    first_mut.ingested_utc = 1000;
    second_mut.ingested_utc = 1001;
    ASSERT_EQ(ss_sdk_db_write_record(&first_mut), SS_SDK_OK);
    ASSERT_EQ(ss_sdk_db_write_record(&second_mut), SS_SDK_OK);

    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_DOUBLE_EQ(out.samples[0].value.f64, 3.0);
    EXPECT_EQ(out.samples[0].flags, SS_SDK_SAMPLE_FORECAST);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, observation_and_forecast_rows_can_coexist_per_slot)
{
    const int64_t slot = now_slot_utc() - 900;
    const ss_sdk_record obs = make_f64_record(
        SS_METRIC_WEATHER_VISIBILITY_KM,
        10.0,
        slot,
        SS_SDK_DATA_OBSERVATION);
    const ss_sdk_record fc = make_f64_record(
        SS_METRIC_WEATHER_VISIBILITY_KM,
        20.0,
        slot,
        SS_SDK_DATA_FORECAST);

    ASSERT_EQ(ss_sdk_db_write_record(&obs), SS_SDK_OK);
    ASSERT_EQ(ss_sdk_db_write_record(&fc), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_VISIBILITY_KM, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_DOUBLE_EQ(out.samples[0].value.f64, 10.0);
    EXPECT_EQ(out.samples[0].flags, SS_SDK_SAMPLE_OBSERVED);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, past_slot_prefers_observation_over_forecast)
{
    const int64_t slot = now_slot_utc() - 3600;
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_WIND_SPEED_10M_MS, 3.0, slot, SS_SDK_DATA_FORECAST), SS_SDK_OK);
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_WIND_SPEED_10M_MS, 2.0, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_WIND_SPEED_10M_MS, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_DOUBLE_EQ(out.samples[0].value.f64, 2.0);
    EXPECT_EQ(out.samples[0].flags, SS_SDK_SAMPLE_OBSERVED);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, past_slot_falls_back_to_forecast_when_observation_missing)
{
    const int64_t slot = now_slot_utc() - 3600;
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_WIND_DIRECTION_10M_DEG, 111.0, slot, SS_SDK_DATA_FORECAST), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_WIND_DIRECTION_10M_DEG, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_DOUBLE_EQ(out.samples[0].value.f64, 111.0);
    EXPECT_EQ(out.samples[0].flags, SS_SDK_SAMPLE_FORECAST);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, now_slot_prefers_observation_over_forecast)
{
    const int64_t slot = now_slot_utc();
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_PRESSURE_MSL_HPA, 1001.0, slot, SS_SDK_DATA_FORECAST), SS_SDK_OK);
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_PRESSURE_MSL_HPA, 1000.0, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_PRESSURE_MSL_HPA, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_DOUBLE_EQ(out.samples[0].value.f64, 1000.0);
    EXPECT_EQ(out.samples[0].flags, SS_SDK_SAMPLE_OBSERVED);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, future_slot_prefers_forecast_even_if_observation_exists)
{
    const int64_t slot = now_slot_utc() + 900;
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT, 70.0, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT, 20.0, slot, SS_SDK_DATA_FORECAST), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_DOUBLE_EQ(out.samples[0].value.f64, 20.0);
    EXPECT_EQ(out.samples[0].flags, SS_SDK_SAMPLE_FORECAST);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, future_slot_with_only_observation_returns_partial)
{
    const int64_t slot = now_slot_utc() + 900;
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_CLOUD_COVER_LOW_PCT, 30.0, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_CLOUD_COVER_LOW_PCT, &out), SS_SDK_ERR_PARTIAL_DATA);
    EXPECT_EQ(out.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out.count, (size_t)0);
}

TEST_F(SdkDbFixture, future_linear_interpolation_does_not_use_observation_rows)
{
    const int64_t now = now_slot_utc();
    const int64_t target = now + 900;
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 2.0, now, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 6.0, now + 1800, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(target, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_ERR_PARTIAL_DATA);
    EXPECT_EQ(out.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out.count, (size_t)0);
}

TEST_F(SdkDbFixture, future_step_metric_accepts_exact_observation_truth)
{
    const int64_t slot = now_slot_utc() + 900;
    ASSERT_EQ(write_f64_record(SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH, 12.34, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_DOUBLE_EQ(out.samples[0].value.f64, 12.34);
    EXPECT_EQ(out.samples[0].flags, SS_SDK_SAMPLE_OBSERVED);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, linear_interpolation_returns_expected_midpoint)
{
    const int64_t base = now_slot_utc() - 7200;
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 1.0, base, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 5.0, base + 3600, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(base + 1800, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_EQ(out.samples[0].flags, SS_SDK_SAMPLE_INTERPOLATED);
    EXPECT_DOUBLE_EQ(out.samples[0].value.f64, 3.0);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, linear_interpolation_length_over_six_hours_returns_partial)
{
    const int64_t base = now_slot_utc() - 36000; /* now - 10h */
    const int64_t target = base + 12600;         /* +3.5h */
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 1.0, base, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 5.0, base + 25200, SS_SDK_DATA_OBSERVATION), SS_SDK_OK); /* +7h */

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(target, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_ERR_PARTIAL_DATA);
    EXPECT_EQ(out.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out.count, (size_t)0);
}

TEST_F(SdkDbFixture, linear_interpolation_without_bracketing_points_returns_partial)
{
    const int64_t base = now_slot_utc() - 7200;
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_HUMIDITY_RELATIVE_2M_PCT, 40.0, base, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(
        ss_sdk_db_get_canonical(base + 900, 1, SS_METRIC_WEATHER_HUMIDITY_RELATIVE_2M_PCT, &out),
        SS_SDK_ERR_PARTIAL_DATA);
    EXPECT_EQ(out.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out.count, (size_t)0);
}

TEST_F(SdkDbFixture, step_interpolation_uses_previous_hour_value)
{
    const int64_t base = now_slot_utc() - 7200;
    ASSERT_EQ(write_f64_record(SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH, 1.0, base, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);
    ASSERT_EQ(write_f64_record(SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH, 5.0, base + 3600, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(base + 1800, 1, SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_EQ(out.samples[0].flags, SS_SDK_SAMPLE_INTERPOLATED);
    EXPECT_DOUBLE_EQ(out.samples[0].value.f64, 1.0);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, step_interpolation_length_over_six_hours_returns_partial)
{
    const int64_t base = now_slot_utc() - 36000;  /* now - 10h */
    const int64_t target = base + 25200;          /* +7h, still in past */
    ASSERT_EQ(write_f64_record(SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH, 1.0, base, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(target, 1, SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH, &out), SS_SDK_ERR_PARTIAL_DATA);
    EXPECT_EQ(out.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out.count, (size_t)0);
}

TEST_F(SdkDbFixture, interpolation_length_over_six_hours_aborts_window_without_partial_subset)
{
    const int64_t base = now_slot_utc() - 36000; /* now - 10h */
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 1.0, base, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 5.0, base + 22500, SS_SDK_DATA_OBSERVATION), SS_SDK_OK); /* +6h15m */

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(
        ss_sdk_db_get_canonical(base, 2, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out),
        SS_SDK_ERR_PARTIAL_DATA);
    EXPECT_EQ(out.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out.count, (size_t)0);
}

TEST_F(SdkDbFixture, no_interpolation_metric_returns_partial_when_missing)
{
    const int64_t base = now_slot_utc() - 7200;
    ASSERT_EQ(write_i64_record(SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE, 1, base, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);
    ASSERT_EQ(write_i64_record(SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE, 5, base + 3600, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(
        ss_sdk_db_get_canonical(base + 1800, 1, SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE, &out),
        SS_SDK_ERR_PARTIAL_DATA);
    EXPECT_EQ(out.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out.count, (size_t)0);
}

TEST_F(SdkDbFixture, returns_partial_data_with_available_subset_in_ts_order)
{
    const int64_t base = now_slot_utc() - 3600;
    ASSERT_EQ(write_i64_record(SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE, 10, base, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);
    ASSERT_EQ(write_i64_record(SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE, 12, base + 1800, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(base, 3, SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE, &out), SS_SDK_ERR_PARTIAL_DATA);
    ASSERT_EQ(out.count, (size_t)2);
    EXPECT_EQ(out.samples[0].ts_utc, base);
    EXPECT_EQ(out.samples[1].ts_utc, base + 1800);

    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, no_rows_returns_partial_with_empty_output)
{
    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(0, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_ERR_PARTIAL_DATA);
    EXPECT_EQ(out.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out.count, (size_t)0);
}

TEST_F(SdkDbFixture, free_samples_is_safe_and_resets_output)
{
    ss_sdk_db_free_samples(NULL);

    const int64_t slot = now_slot_utc() - 900;
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_WIND_GUST_10M_MS, 9.0, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_WIND_GUST_10M_MS, &out), SS_SDK_OK);
    ASSERT_NE(out.samples, (ss_sdk_sample *)NULL);
    ASSERT_EQ(out.count, (size_t)1);

    ss_sdk_db_free_samples(&out);
    EXPECT_EQ(out.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out.count, (size_t)0);
}

TEST_F(SdkDbFixture, get_canonical_resets_out_on_error_paths)
{
    ss_sdk_samples_out out;
    out.samples = (ss_sdk_sample *)0x1;
    out.count = 123;

    ASSERT_EQ(ss_sdk_db_get_canonical(-1, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_ERR_INVALID_ARG);
    EXPECT_EQ(out.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out.count, (size_t)0);
}

TEST_F(SdkDbFixture, switch_db_dir_uses_new_database)
{
    const int64_t slot = now_slot_utc() - 900;
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_WIND_SPEED_10M_MS, 4.0, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_samples_out out_first = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_WIND_SPEED_10M_MS, &out_first), SS_SDK_OK);
    ASSERT_EQ(out_first.count, (size_t)1);
    ss_sdk_db_free_samples(&out_first);

    const std::string dir_two = dir_ + "/db_two";
    ASSERT_EQ(mkdir(dir_two.c_str(), 0775), 0);
    ASSERT_EQ(setenv("SS_SDK_DB_DIR", dir_two.c_str(), 1), 0);

    ss_sdk_samples_out out_second = {NULL, 0};
    ASSERT_EQ(
        ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_WIND_SPEED_10M_MS, &out_second),
        SS_SDK_ERR_PARTIAL_DATA);
    EXPECT_EQ(out_second.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out_second.count, (size_t)0);

    remove_file_if_exists(dir_two + "/59329300_18068600.db");
    remove_dir_if_exists(dir_two);
}

TEST_F(SdkDbFixture, sdk_debug_level_logs_selected_db_path_for_write)
{
    const int64_t slot = now_slot_utc();
    const std::string log_path = dir_ + "/sdk_debug.log";

    ASSERT_EQ(setenv("SS_SDK_LOG_LEVEL", "debug", 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "1", 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_PATH", log_path.c_str(), 1), 0);

    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 5.5, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    const std::string text = read_text_file(log_path);
    EXPECT_NE(text.find("sdk.db.path.selected"), std::string::npos);
    EXPECT_NE(text.find("db_path=" + db_path_), std::string::npos);
}

TEST_F(SdkDbFixture, sdk_debug_level_logs_selected_db_path_for_read)
{
    const int64_t slot = now_slot_utc();
    const std::string log_path = dir_ + "/sdk_debug_read.log";
    ss_sdk_samples_out out = {NULL, 0};

    ASSERT_EQ(setenv("SS_SDK_LOG_LEVEL", "debug", 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "1", 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_PATH", log_path.c_str(), 1), 0);

    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 4.0, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_OK);
    ss_sdk_db_free_samples(&out);

    const std::string text = read_text_file(log_path);
    EXPECT_NE(text.find("sdk.db.path.selected"), std::string::npos);
    EXPECT_NE(text.find("db_path=" + db_path_), std::string::npos);
}

#ifdef SS_SDK_ENABLE_TEST_HOOKS
TEST_F(SdkDbFixture, test_helpers_cover_path_argument_guards)
{
    ASSERT_EQ(ss_sdk_internal_db_test_ensure_parent_dirs(NULL), -1);
    EXPECT_EQ(errno, EINVAL);
    ASSERT_EQ(ss_sdk_internal_db_test_ensure_parent_dirs(""), -1);
    EXPECT_EQ(errno, EINVAL);

    ASSERT_EQ(ss_sdk_internal_db_test_strdup_local(NULL), (char *)NULL);

    EXPECT_STREQ(ss_sdk_internal_db_test_db_path(), db_path_.c_str());
}

TEST_F(SdkDbFixture, test_hooks_force_internal_error_paths)
{
    const int64_t slot = now_slot_utc() - 1800;
    ss_sdk_samples_out out = {NULL, 0};

    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_STRDUP, 1);
    EXPECT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 1.0, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_MKDIR, 1);
    EXPECT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 1.0, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_SQLITE_OPEN, 1);
    EXPECT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 1.0, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_SQLITE_EXEC, 1);
    EXPECT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 1.0, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_ERR_INTERNAL);

    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 2.0, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_SQLITE_PREPARE, 1);
    EXPECT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_SQLITE_STEP, 1);
    EXPECT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_REALLOC, 1);
    EXPECT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_CALLOC, 1);
    EXPECT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_ERR_INTERNAL);
}

TEST_F(SdkDbFixture, test_hooks_cover_interpolation_and_now_edges)
{
    const int64_t slot = now_slot_utc();
    ss_sdk_samples_out out = {NULL, 0};

    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FORCE_INTERPOLATION_MAP_INCOMPLETE, 1);
    EXPECT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FORCE_NOW_NEGATIVE, 1);
    EXPECT_EQ(ss_sdk_internal_db_test_now_slot_utc(), (int64_t)0);

    ss_sdk_internal_db_test_reset_hooks();
    EXPECT_TRUE(ss_sdk_internal_db_test_interpolation_map_complete());
}

TEST_F(SdkDbFixture, shutdown_recovers_when_sqlite_close_reports_busy)
{
    const int64_t slot = now_slot_utc() - 900;
    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 10.0, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_SQLITE_CLOSE, 1);
    ss_sdk_shutdown();

    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(slot, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    ss_sdk_db_free_samples(&out);
}

TEST_F(SdkDbFixture, internal_get_handles_large_query_end_without_overflow)
{
    ss_sdk_samples_out out = {NULL, 0};
    const int64_t end = std::numeric_limits<int64_t>::max() - 900;
    const int64_t start = end - 900;

    ASSERT_EQ(
        ss_sdk_internal_db_get_canonical(
            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
            start,
            end,
            &out),
        SS_SDK_ERR_PARTIAL_DATA);
    EXPECT_EQ(out.samples, (ss_sdk_sample *)NULL);
    EXPECT_EQ(out.count, (size_t)0);
}

TEST_F(SdkDbFixture, public_now_slot_negative_branch_uses_zero)
{
    const ss_sdk_record rec = make_f64_record(
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        6.0,
        0,
        SS_SDK_DATA_OBSERVATION);
    ASSERT_EQ(ss_sdk_db_write_record(&rec), SS_SDK_OK);

    ss_sdk_test_set_now_override(1, -1);
    ss_sdk_samples_out out = {NULL, 0};
    ASSERT_EQ(ss_sdk_db_get_canonical(0, 1, SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, &out), SS_SDK_OK);
    ASSERT_EQ(out.count, (size_t)1);
    EXPECT_EQ(out.samples[0].ts_utc, (int64_t)0);
    ss_sdk_db_free_samples(&out);
    ss_sdk_test_set_now_override(0, 0);
}

TEST_F(SdkDbFixture, test_helpers_cover_additional_internal_branches)
{
    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook((ss_sdk_db_test_hook)999, 1);

    EXPECT_EQ(ss_sdk_internal_db_test_consume_null_slot(), 0);
    EXPECT_EQ(ss_sdk_internal_db_test_interpolation_policy((ss_metric_id)SS_METRIC_COUNT), -1);
    EXPECT_FALSE(ss_sdk_internal_db_test_raw_rows_append_overflow());
    EXPECT_EQ(
        ss_sdk_internal_db_test_load_rows_for_window(
            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
            SS_SDK_VALUE_F64,
            0,
            900,
            NULL),
        SS_SDK_ERR_INVALID_ARG);

    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_STRDUP, 1);
    EXPECT_EQ(ss_sdk_internal_db_test_strdup_local("x"), (char *)NULL);
    EXPECT_EQ(errno, ENOMEM);
    ss_sdk_internal_db_test_reset_hooks();

    EXPECT_FALSE(ss_sdk_internal_db_test_try_interpolate_linear_nonf64());
    EXPECT_FALSE(ss_sdk_internal_db_test_try_interpolate_zero_span());
    EXPECT_FALSE(ss_sdk_internal_db_test_try_interpolate_unknown_policy());
}

TEST_F(SdkDbFixture, internal_loader_returns_all_forecast_versions_for_slot)
{
    const int64_t slot = now_slot_utc() + 3600;
    size_t out_count = 0U;
    ss_sdk_record first = make_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 1.0, slot, SS_SDK_DATA_FORECAST);
    ss_sdk_record second = make_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 2.0, slot, SS_SDK_DATA_FORECAST);

    first.ingested_utc = 1000;
    second.ingested_utc = 1001;
    ASSERT_EQ(ss_sdk_db_write_record(&first), SS_SDK_OK);
    ASSERT_EQ(ss_sdk_db_write_record(&second), SS_SDK_OK);

    ASSERT_EQ(
        ss_sdk_internal_db_test_load_rows_for_window(
            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
            SS_SDK_VALUE_F64,
            slot,
            slot + 900,
            &out_count),
        SS_SDK_OK);
    EXPECT_EQ(out_count, (size_t)2);
}

TEST_F(SdkDbFixture, interpolation_policy_table_keeps_expected_metric_categories)
{
    const int k_policy_none = 0;
    const int k_policy_linear = 1;
    const int k_policy_step = 2;
    const ss_metric_id linear_metrics[] = {
        SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
        SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2,
        SS_METRIC_ENERGY_FX_SEK_PER_EUR};
    const ss_metric_id step_metrics[] = {
        SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH,
        SS_METRIC_ENERGY_PRICE_SPOT_EUR_KWH};
    const ss_metric_id none_metrics[] = {
        SS_METRIC_WEATHER_CONDITION_SYMBOL_CODE,
        SS_METRIC_WEATHER_IS_DAY};
    size_t i;

    for (i = 0; i < sizeof(linear_metrics) / sizeof(linear_metrics[0]); ++i) {
        EXPECT_EQ(ss_sdk_internal_db_test_interpolation_policy(linear_metrics[i]), k_policy_linear);
    }
    for (i = 0; i < sizeof(step_metrics) / sizeof(step_metrics[0]); ++i) {
        EXPECT_EQ(ss_sdk_internal_db_test_interpolation_policy(step_metrics[i]), k_policy_step);
    }
    for (i = 0; i < sizeof(none_metrics) / sizeof(none_metrics[0]); ++i) {
        EXPECT_EQ(ss_sdk_internal_db_test_interpolation_policy(none_metrics[i]), k_policy_none);
    }
}

TEST_F(SdkDbFixture, load_rows_prepare_or_step_failure_is_recoverable)
{
    const int64_t slot = now_slot_utc() - 1800;
    size_t out_count = 0;

    ASSERT_EQ(write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 4.25, slot, SS_SDK_DATA_OBSERVATION), SS_SDK_OK);

    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_SQLITE_STEP, 1);
    EXPECT_EQ(
        ss_sdk_internal_db_test_load_rows_for_window(
            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
            SS_SDK_VALUE_F64,
            slot,
            slot + 900,
            &out_count),
        SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_db_test_reset_hooks();
    EXPECT_EQ(
        ss_sdk_internal_db_test_load_rows_for_window(
            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
            SS_SDK_VALUE_F64,
            slot,
            slot + 900,
            &out_count),
        SS_SDK_OK);
    EXPECT_EQ(out_count, (size_t)1);
}

TEST_F(SdkDbFixture, db_open_error_variants_cover_pragmas_schema_strdup_and_open_failure)
{
    const int64_t slot = now_slot_utc() - 4500;

    for (int n = 2; n <= 5; ++n) {
        ss_sdk_shutdown();
        ss_sdk_internal_db_test_reset_hooks();
        ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_SQLITE_EXEC, n);
        EXPECT_EQ(
            write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 1.0 + (double)n, slot + (int64_t)n * 900, SS_SDK_DATA_OBSERVATION),
            SS_SDK_ERR_INTERNAL);
    }

    ss_sdk_shutdown();
    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_STRDUP, 2);
    EXPECT_EQ(
        write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 9.0, slot, SS_SDK_DATA_OBSERVATION),
        SS_SDK_ERR_INTERNAL);

    ss_sdk_shutdown();
    ASSERT_EQ(setenv("SS_SDK_DB_DIR", "/", 1), 0);
    EXPECT_EQ(
        write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 3.0, slot + 9000, SS_SDK_DATA_OBSERVATION),
        SS_SDK_ERR_INTERNAL);
    ASSERT_EQ(setenv("SS_SDK_DB_DIR", db_dir_.c_str(), 1), 0);
}

TEST_F(SdkDbFixture, internal_exec_and_parent_dir_failures_are_exercised)
{
    const std::string bad_parent = dir_ + "/parent_is_file";
    FILE *fp = std::fopen(bad_parent.c_str(), "w");
    ASSERT_NE(fp, nullptr);
    ASSERT_EQ(std::fclose(fp), 0);

    const std::string nested = bad_parent + "/child/sdk.db";
    EXPECT_EQ(ss_sdk_internal_db_test_ensure_parent_dirs(nested.c_str()), -1);

    EXPECT_EQ(ss_sdk_internal_db_test_exec_sql(NULL), SS_SDK_ERR_INVALID_ARG);
    EXPECT_EQ(ss_sdk_internal_db_test_exec_sql("SELECT 1;"), SS_SDK_OK);
    EXPECT_EQ(ss_sdk_internal_db_test_exec_sql("THIS IS NOT VALID SQL;"), SS_SDK_ERR_INTERNAL);

    remove_file_if_exists(bad_parent);
}

TEST_F(SdkDbFixture, internal_write_and_get_cover_remaining_error_paths)
{
    const int64_t slot = now_slot_utc() - 6300;
    ss_sdk_samples_out out = {NULL, 0};
    ss_sdk_internal_db_test_reset_hooks();

    EXPECT_EQ(ss_sdk_internal_db_write_record(NULL), SS_SDK_ERR_INVALID_ARG);

    ss_sdk_record rec = make_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 12.0, slot, SS_SDK_DATA_OBSERVATION);
    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_SQLITE_PREPARE, 1);
    EXPECT_EQ(ss_sdk_internal_db_write_record(&rec), SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_SQLITE_STEP, 1);
    EXPECT_EQ(ss_sdk_internal_db_write_record(&rec), SS_SDK_ERR_INTERNAL);

    ss_sdk_record bool_rec = rec;
    bool_rec.ts_start_utc = slot + 900;
    bool_rec.ts_end_utc = bool_rec.ts_start_utc + 900;
    bool_rec.value_type = SS_SDK_VALUE_BOOL;
    bool_rec.value.boolean = true;
    EXPECT_EQ(ss_sdk_internal_db_write_record(&bool_rec), SS_SDK_OK);

    ss_sdk_record bad_type = rec;
    bad_type.ts_start_utc = slot + 1800;
    bad_type.ts_end_utc = bad_type.ts_start_utc + 900;
    bad_type.value_type = (ss_sdk_value_type)99;
    EXPECT_EQ(ss_sdk_internal_db_write_record(&bad_type), SS_SDK_ERR_VALIDATION);

    ss_sdk_record bad_constraint = rec;
    bad_constraint.ts_start_utc = slot + 2700;
    bad_constraint.ts_end_utc = bad_constraint.ts_start_utc + 1800;
    EXPECT_EQ(ss_sdk_internal_db_write_record(&bad_constraint), SS_SDK_ERR_VALIDATION);

    EXPECT_EQ(
        ss_sdk_internal_db_get_canonical(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, slot, slot + 900, NULL),
        SS_SDK_ERR_INVALID_ARG);
    EXPECT_EQ(
        ss_sdk_internal_db_get_canonical(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, slot + 900, slot, &out),
        SS_SDK_ERR_INVALID_ARG);
    EXPECT_EQ(
        ss_sdk_internal_db_get_canonical(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, slot, slot + 901, &out),
        SS_SDK_ERR_INVALID_ARG);
    EXPECT_EQ(
        ss_sdk_internal_db_get_canonical((ss_metric_id)SS_METRIC_COUNT, slot, slot + 900, &out),
        SS_SDK_ERR_INVALID_ARG);

    ss_sdk_shutdown();
    ss_sdk_internal_db_test_reset_hooks();
    ss_sdk_internal_db_test_set_hook(SS_SDK_DB_HOOK_FAIL_SQLITE_OPEN, 1);
    EXPECT_EQ(
        ss_sdk_internal_db_get_canonical(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, slot, slot + 900, &out),
        SS_SDK_ERR_INTERNAL);
    ss_sdk_db_free_samples(&out);
    ss_sdk_internal_db_test_reset_hooks();
}

TEST_F(SdkDbFixture, internal_row_loader_filters_invalid_rows_for_all_value_types)
{
    const int metric = (int)SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C;
    const int64_t base = now_slot_utc() - 10800;
    size_t out_count = 0;
    ss_sdk_internal_db_test_reset_hooks();

    ASSERT_EQ(
        write_f64_record(SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C, 1.5, base, SS_SDK_DATA_OBSERVATION),
        SS_SDK_OK);

    std::string sql = "PRAGMA ignore_check_constraints = ON;";
    sql += "INSERT INTO records(canonical,value_type,value_i64,value_f64,value_bool,ts_start_utc,ts_end_utc,data_kind,ingested_utc) VALUES(" +
           std::to_string(metric) + ",1,NULL,2.0,NULL,-900,0,0,1);";
    sql += "INSERT INTO records(canonical,value_type,value_i64,value_f64,value_bool,ts_start_utc,ts_end_utc,data_kind,ingested_utc) VALUES(" +
           std::to_string(metric) + ",1,NULL,2.0,NULL," + std::to_string(base + 1) + "," + std::to_string(base + 901) + ",0,1);";
    sql += "INSERT INTO records(canonical,value_type,value_i64,value_f64,value_bool,ts_start_utc,ts_end_utc,data_kind,ingested_utc) VALUES(" +
           std::to_string(metric) + ",1,NULL,2.0,NULL," + std::to_string(base + 900) + "," + std::to_string(base + 1800) + ",9,1);";
    sql += "INSERT INTO records(canonical,value_type,value_i64,value_f64,value_bool,ts_start_utc,ts_end_utc,data_kind,ingested_utc) VALUES(" +
           std::to_string(metric) + ",0,7,NULL,NULL," + std::to_string(base + 1800) + "," + std::to_string(base + 2700) + ",0,1);";
    sql += "INSERT INTO records(canonical,value_type,value_i64,value_f64,value_bool,ts_start_utc,ts_end_utc,data_kind,ingested_utc) VALUES(" +
           std::to_string(metric) + ",1,NULL,NULL,NULL," + std::to_string(base + 2700) + "," + std::to_string(base + 3600) + ",0,1);";
    sql += "INSERT INTO records(canonical,value_type,value_i64,value_f64,value_bool,ts_start_utc,ts_end_utc,data_kind,ingested_utc) VALUES(" +
           std::to_string(metric) + ",1,NULL,NULL,NULL," + std::to_string(base + 3600) + "," + std::to_string(base + 4500) + ",0,1);";
    sql += "INSERT INTO records(canonical,value_type,value_i64,value_f64,value_bool,ts_start_utc,ts_end_utc,data_kind,ingested_utc) VALUES(" +
           std::to_string(metric) + ",0,NULL,NULL,NULL," + std::to_string(base + 5400) + "," + std::to_string(base + 6300) + ",0,1);";
    sql += "INSERT INTO records(canonical,value_type,value_i64,value_f64,value_bool,ts_start_utc,ts_end_utc,data_kind,ingested_utc) VALUES(" +
           std::to_string(metric) + ",2,NULL,NULL,NULL," + std::to_string(base + 6300) + "," + std::to_string(base + 7200) + ",0,1);";
    sql += "INSERT INTO records(canonical,value_type,value_i64,value_f64,value_bool,ts_start_utc,ts_end_utc,data_kind,ingested_utc) VALUES(" +
           std::to_string(metric) + ",2,NULL,NULL,2," + std::to_string(base + 7200) + "," + std::to_string(base + 8100) + ",0,1);";
    sql += "INSERT INTO records(canonical,value_type,value_i64,value_f64,value_bool,ts_start_utc,ts_end_utc,data_kind,ingested_utc) VALUES(" +
           std::to_string(metric) + ",2,NULL,NULL,1," + std::to_string(base + 8100) + "," + std::to_string(base + 9000) + ",0,1);";
    ASSERT_EQ(ss_sdk_internal_db_test_exec_sql(sql.c_str()), SS_SDK_OK);

    ASSERT_EQ(
        ss_sdk_internal_db_test_load_rows_for_window(
            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
            SS_SDK_VALUE_F64,
            base,
            base + 4500,
            &out_count),
        SS_SDK_OK);
    EXPECT_EQ(out_count, (size_t)1);

    ASSERT_EQ(
        ss_sdk_internal_db_test_load_rows_for_window(
            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
            SS_SDK_VALUE_I64,
            base + 25200,
            base + 26100,
            &out_count),
        SS_SDK_OK);
    EXPECT_EQ(out_count, (size_t)0);

    ASSERT_EQ(
        ss_sdk_internal_db_test_load_rows_for_window(
            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
            SS_SDK_VALUE_BOOL,
            base + 6300,
            base + 9000,
            &out_count),
        SS_SDK_OK);
    EXPECT_EQ(out_count, (size_t)1);
    ss_sdk_internal_db_test_reset_hooks();
}
#endif
