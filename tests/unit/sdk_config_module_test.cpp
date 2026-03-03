#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

extern "C" {
#include "sdk/internal/ss_sdk_config.h"
#include "sdk/internal/ss_sdk_config_util.h"
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

class SdkConfigFixture : public ::testing::Test {
protected:
    SdkConfigFixture()
        : cfg_guard_("SUNSPOTS_CONFIG"),
          db_dir_guard_(SS_SDK_ENV_DB_DIR),
          level_guard_(SS_SDK_ENV_LOG_LEVEL),
          mirror_enabled_guard_(SS_SDK_ENV_LOG_MIRROR_ENABLED),
          mirror_path_guard_(SS_SDK_ENV_LOG_MIRROR_PATH),
          mirror_max_bytes_guard_(SS_SDK_ENV_LOG_MIRROR_MAX_BYTES),
          backfill_run_once_guard_(SS_BACKFILL_ENV_RUN_ONCE),
          system_guard_("SUNSPOTS_SYSTEM")
    {}

    void clear_sdk_env()
    {
        ASSERT_EQ(unsetenv(SS_SDK_ENV_DB_DIR), 0);
        ASSERT_EQ(unsetenv(SS_SDK_ENV_LOG_LEVEL), 0);
        ASSERT_EQ(unsetenv(SS_SDK_ENV_LOG_MIRROR_ENABLED), 0);
        ASSERT_EQ(unsetenv(SS_SDK_ENV_LOG_MIRROR_PATH), 0);
        ASSERT_EQ(unsetenv(SS_SDK_ENV_LOG_MIRROR_MAX_BYTES), 0);
        ASSERT_EQ(unsetenv(SS_BACKFILL_ENV_RUN_ONCE), 0);
    }

    ScopedEnvVar cfg_guard_;
    ScopedEnvVar db_dir_guard_;
    ScopedEnvVar level_guard_;
    ScopedEnvVar mirror_enabled_guard_;
    ScopedEnvVar mirror_path_guard_;
    ScopedEnvVar mirror_max_bytes_guard_;
    ScopedEnvVar backfill_run_once_guard_;
    ScopedEnvVar system_guard_;
};

}  // namespace

TEST_F(SdkConfigFixture, sdk_env_vars_bootstrap_from_system_sdk_block)
{
    const char *cfg =
        "{"
        "\"name\":\"ConfigBootstrapTest\","
        "\"system\":{"
        "\"sdk\":{"
        "\"db_dir\":\"db/system_bootstrap\","
        "\"log_level\":\"info\","
        "\"log_mirror_enabled\":true,"
        "\"log_mirror_path\":\"logs/system_bootstrap.log\","
        "\"log_mirror_max_bytes\":\"2048\""
        "}"
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg, 1), 0);
    clear_sdk_env();

    ss_sdk_config_bootstrap_env_from_blob();

    ASSERT_NE(std::getenv(SS_SDK_ENV_DB_DIR), nullptr);
    ASSERT_NE(std::getenv(SS_SDK_ENV_LOG_LEVEL), nullptr);
    ASSERT_NE(std::getenv(SS_SDK_ENV_LOG_MIRROR_ENABLED), nullptr);
    ASSERT_NE(std::getenv(SS_SDK_ENV_LOG_MIRROR_PATH), nullptr);
    ASSERT_NE(std::getenv(SS_SDK_ENV_LOG_MIRROR_MAX_BYTES), nullptr);
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_DB_DIR), "db/system_bootstrap");
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_LOG_LEVEL), "info");
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_ENABLED), "1");
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_PATH), "logs/system_bootstrap.log");
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_MAX_BYTES), "2048");
}

TEST_F(SdkConfigFixture, bootstrap_does_not_override_daemon_exported_env_vars)
{
    const char *cfg =
        "{"
        "\"name\":\"ConfigBootstrapTest\","
        "\"system\":{"
        "\"sdk\":{"
        "\"db_dir\":\"db/from_config\","
        "\"log_level\":\"debug\","
        "\"log_mirror_enabled\":true,"
        "\"log_mirror_path\":\"logs/from_config.log\","
        "\"log_mirror_max_bytes\":\"8192\""
        "}"
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg, 1), 0);
    ASSERT_EQ(setenv(SS_SDK_ENV_DB_DIR, "db/from_daemon", 1), 0);
    ASSERT_EQ(setenv(SS_SDK_ENV_LOG_LEVEL, "error", 1), 0);
    ASSERT_EQ(setenv(SS_SDK_ENV_LOG_MIRROR_ENABLED, "0", 1), 0);
    ASSERT_EQ(setenv(SS_SDK_ENV_LOG_MIRROR_PATH, "logs/from_daemon.log", 1), 0);
    ASSERT_EQ(setenv(SS_SDK_ENV_LOG_MIRROR_MAX_BYTES, "1024", 1), 0);

    ss_sdk_config_bootstrap_env_from_blob();

    EXPECT_STREQ(std::getenv(SS_SDK_ENV_DB_DIR), "db/from_daemon");
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_LOG_LEVEL), "error");
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_ENABLED), "0");
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_PATH), "logs/from_daemon.log");
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_MAX_BYTES), "1024");
}

TEST_F(SdkConfigFixture, common_sdk_block_is_ignored)
{
    const char *cfg =
        "{"
        "\"name\":\"ConfigBootstrapTest\","
        "\"common\":{"
        "\"sdk\":{"
        "\"db_dir\":\"db/common_bootstrap\","
        "\"log_level\":\"warn\","
        "\"log_mirror_enabled\":true,"
        "\"log_mirror_path\":\"logs/common_bootstrap.log\","
        "\"log_mirror_max_bytes\":\"3072\""
        "}"
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg, 1), 0);
    clear_sdk_env();

    ss_sdk_config_bootstrap_env_from_blob();

    EXPECT_EQ(std::getenv(SS_SDK_ENV_DB_DIR), nullptr);
    EXPECT_EQ(std::getenv(SS_SDK_ENV_LOG_LEVEL), nullptr);
    EXPECT_EQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_ENABLED), nullptr);
    EXPECT_EQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_PATH), nullptr);
    EXPECT_EQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_MAX_BYTES), nullptr);
}

TEST_F(SdkConfigFixture, root_sdk_block_is_ignored)
{
    const char *cfg =
        "{"
        "\"name\":\"ConfigBootstrapTest\","
        "\"sdk\":{"
        "\"db_dir\":\"db/root_bootstrap\","
        "\"log_level\":\"warn\","
        "\"log_mirror_enabled\":false,"
        "\"log_mirror_path\":\"logs/root_bootstrap.log\","
        "\"log_mirror_max_bytes\":\"4096\""
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg, 1), 0);
    clear_sdk_env();

    ss_sdk_config_bootstrap_env_from_blob();

    EXPECT_EQ(std::getenv(SS_SDK_ENV_DB_DIR), nullptr);
    EXPECT_EQ(std::getenv(SS_SDK_ENV_LOG_LEVEL), nullptr);
    EXPECT_EQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_ENABLED), nullptr);
    EXPECT_EQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_PATH), nullptr);
    EXPECT_EQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_MAX_BYTES), nullptr);
}

TEST_F(SdkConfigFixture, bootstrap_sets_backfill_run_one_env_var)
{
    const char *cfg =
        "{"
        "\"name\":\"ConfigBootstrapTest\","
        "\"backfill\":{"
        "\"run_one\":true"
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg, 1), 0);
    ASSERT_EQ(unsetenv(SS_BACKFILL_ENV_RUN_ONCE), 0);

    ss_sdk_config_bootstrap_env_from_blob();

    ASSERT_NE(std::getenv(SS_BACKFILL_ENV_RUN_ONCE), nullptr);
    EXPECT_STREQ(std::getenv(SS_BACKFILL_ENV_RUN_ONCE), "1");
}

TEST_F(SdkConfigFixture, bootstrap_does_not_override_existing_backfill_run_one_env_var)
{
    const char *cfg =
        "{"
        "\"name\":\"ConfigBootstrapTest\","
        "\"backfill\":{"
        "\"run_one\":false"
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg, 1), 0);
    ASSERT_EQ(setenv(SS_BACKFILL_ENV_RUN_ONCE, "1", 1), 0);

    ss_sdk_config_bootstrap_env_from_blob();

    ASSERT_NE(std::getenv(SS_BACKFILL_ENV_RUN_ONCE), nullptr);
    EXPECT_STREQ(std::getenv(SS_BACKFILL_ENV_RUN_ONCE), "1");
}

TEST_F(SdkConfigFixture, bootstrap_sets_backfill_run_once_alias_env_var)
{
    const char *cfg =
        "{"
        "\"name\":\"ConfigBootstrapTest\","
        "\"backfill\":{"
        "\"run_once\":true"
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg, 1), 0);
    ASSERT_EQ(unsetenv(SS_BACKFILL_ENV_RUN_ONCE), 0);

    ss_sdk_config_bootstrap_env_from_blob();

    ASSERT_NE(std::getenv(SS_BACKFILL_ENV_RUN_ONCE), nullptr);
    EXPECT_STREQ(std::getenv(SS_BACKFILL_ENV_RUN_ONCE), "1");
}

TEST_F(SdkConfigFixture, bootstrap_ignores_malformed_json_blob)
{
    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", "{bad-json", 1), 0);
    clear_sdk_env();

    ss_sdk_config_bootstrap_env_from_blob();

    EXPECT_EQ(std::getenv(SS_SDK_ENV_DB_DIR), nullptr);
    EXPECT_EQ(std::getenv(SS_SDK_ENV_LOG_LEVEL), nullptr);
    EXPECT_EQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_ENABLED), nullptr);
    EXPECT_EQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_PATH), nullptr);
    EXPECT_EQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_MAX_BYTES), nullptr);
}

TEST_F(SdkConfigFixture, cfg_util_rejects_invalid_dot_paths)
{
    int out_bool = 0;
    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", "{\"system\":{\"sdk\":{\"log_mirror_enabled\":true}}}", 1), 0);

    EXPECT_EQ(
        ss_sdk_cfg_get_bool_from_env_json("SUNSPOTS_CONFIG", "system..sdk.log_mirror_enabled", &out_bool),
        SS_SDK_CFG_INVALID_ARG);
    EXPECT_EQ(
        ss_sdk_cfg_get_bool_from_env_json("SUNSPOTS_CONFIG", "system.sdk.", &out_bool),
        SS_SDK_CFG_INVALID_ARG);
}

TEST_F(SdkConfigFixture, cfg_util_int_range_and_type_validation)
{
    int out_int = 0;

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", "{\"n\":11,\"s\":\"11\",\"f\":11.8}", 1), 0);
    EXPECT_EQ(ss_sdk_cfg_get_int_from_env_json("SUNSPOTS_CONFIG", "n", 0, 10, &out_int), SS_SDK_CFG_RANGE_ERR);
    EXPECT_EQ(ss_sdk_cfg_get_int_from_env_json("SUNSPOTS_CONFIG", "s", 0, 20, &out_int), SS_SDK_CFG_TYPE_MISMATCH);
    EXPECT_EQ(ss_sdk_cfg_get_int_from_env_json("SUNSPOTS_CONFIG", "f", 0, 20, &out_int), SS_SDK_CFG_OK);
    EXPECT_EQ(out_int, 11);
}

TEST_F(SdkConfigFixture, cfg_util_string_bounds_and_empty_handling)
{
    char out_small[4];
    char out_ok[16];

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", "{\"empty\":\"\",\"name\":\"abcdef\"}", 1), 0);
    EXPECT_EQ(ss_sdk_cfg_get_string_from_env_json("SUNSPOTS_CONFIG", "empty", out_ok, sizeof(out_ok)), SS_SDK_CFG_NOT_FOUND);
    EXPECT_EQ(ss_sdk_cfg_get_string_from_env_json("SUNSPOTS_CONFIG", "name", out_small, sizeof(out_small)), SS_SDK_CFG_RANGE_ERR);
    EXPECT_EQ(ss_sdk_cfg_get_string_from_env_json("SUNSPOTS_CONFIG", "name", out_ok, sizeof(out_ok)), SS_SDK_CFG_OK);
    EXPECT_STREQ(out_ok, "abcdef");
}

TEST_F(SdkConfigFixture, cfg_util_location_success_and_missing_required_fields)
{
    ss_sdk_cfg_location loc;

    ASSERT_EQ(
        setenv(
            "SUNSPOTS_SYSTEM",
            "{\"location\":{\"name\":\"X\",\"latitude\":59.3,\"longitude\":18.0,\"elprisomrade\":\"SE3\"}}",
            1),
        0);
    EXPECT_EQ(ss_sdk_cfg_get_location_from_system_env(&loc), SS_SDK_CFG_OK);
    EXPECT_DOUBLE_EQ(loc.latitude, 59.3);
    EXPECT_DOUBLE_EQ(loc.longitude, 18.0);
    EXPECT_STREQ(loc.name, "X");
    EXPECT_STREQ(loc.elprisomrade, "SE3");

    ASSERT_EQ(setenv("SUNSPOTS_SYSTEM", "{\"location\":{\"longitude\":18.0}}", 1), 0);
    EXPECT_EQ(ss_sdk_cfg_get_location_from_system_env(&loc), SS_SDK_CFG_NOT_FOUND);

    ASSERT_EQ(setenv("SUNSPOTS_SYSTEM", "{\"location\":{\"latitude\":59.3}}", 1), 0);
    EXPECT_EQ(ss_sdk_cfg_get_location_from_system_env(&loc), SS_SDK_CFG_NOT_FOUND);
}
