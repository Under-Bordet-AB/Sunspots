#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

extern "C" {
#include "sdk/internal/ss_sdk_config.h"
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
          db_guard_(SS_SDK_ENV_DB_PATH),
          level_guard_(SS_SDK_ENV_LOG_LEVEL),
          mirror_enabled_guard_(SS_SDK_ENV_LOG_MIRROR_ENABLED),
          mirror_path_guard_(SS_SDK_ENV_LOG_MIRROR_PATH)
    {}

    void clear_sdk_env()
    {
        ASSERT_EQ(unsetenv(SS_SDK_ENV_DB_PATH), 0);
        ASSERT_EQ(unsetenv(SS_SDK_ENV_LOG_LEVEL), 0);
        ASSERT_EQ(unsetenv(SS_SDK_ENV_LOG_MIRROR_ENABLED), 0);
        ASSERT_EQ(unsetenv(SS_SDK_ENV_LOG_MIRROR_PATH), 0);
    }

    ScopedEnvVar cfg_guard_;
    ScopedEnvVar db_guard_;
    ScopedEnvVar level_guard_;
    ScopedEnvVar mirror_enabled_guard_;
    ScopedEnvVar mirror_path_guard_;
};

}  // namespace

TEST_F(SdkConfigFixture, sdk_env_vars_bootstrap_from_config_blob)
{
    const char *cfg =
        "{"
        "\"name\":\"ConfigBootstrapTest\","
        "\"common\":{"
        "\"sdk\":{"
        "\"db_path\":\"db/test_bootstrap.db\","
        "\"log_level\":\"warn\","
        "\"log_mirror_enabled\":false,"
        "\"log_mirror_path\":\"logs/test_bootstrap.log\""
        "}"
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg, 1), 0);
    clear_sdk_env();

    ss_sdk_config_bootstrap_env_from_blob();

    ASSERT_NE(std::getenv(SS_SDK_ENV_DB_PATH), nullptr);
    ASSERT_NE(std::getenv(SS_SDK_ENV_LOG_LEVEL), nullptr);
    ASSERT_NE(std::getenv(SS_SDK_ENV_LOG_MIRROR_ENABLED), nullptr);
    ASSERT_NE(std::getenv(SS_SDK_ENV_LOG_MIRROR_PATH), nullptr);
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_DB_PATH), "db/test_bootstrap.db");
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_LOG_LEVEL), "warn");
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_ENABLED), "0");
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_PATH), "logs/test_bootstrap.log");
}

TEST_F(SdkConfigFixture, bootstrap_does_not_override_daemon_exported_env_vars)
{
    const char *cfg =
        "{"
        "\"name\":\"ConfigBootstrapTest\","
        "\"common\":{"
        "\"sdk\":{"
        "\"db_path\":\"db/from_config.db\","
        "\"log_level\":\"debug\","
        "\"log_mirror_enabled\":true,"
        "\"log_mirror_path\":\"logs/from_config.log\""
        "}"
        "}"
        "}";

    ASSERT_EQ(setenv("SUNSPOTS_CONFIG", cfg, 1), 0);
    ASSERT_EQ(setenv(SS_SDK_ENV_DB_PATH, "db/from_daemon.db", 1), 0);
    ASSERT_EQ(setenv(SS_SDK_ENV_LOG_LEVEL, "error", 1), 0);
    ASSERT_EQ(setenv(SS_SDK_ENV_LOG_MIRROR_ENABLED, "0", 1), 0);
    ASSERT_EQ(setenv(SS_SDK_ENV_LOG_MIRROR_PATH, "logs/from_daemon.log", 1), 0);

    ss_sdk_config_bootstrap_env_from_blob();

    EXPECT_STREQ(std::getenv(SS_SDK_ENV_DB_PATH), "db/from_daemon.db");
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_LOG_LEVEL), "error");
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_ENABLED), "0");
    EXPECT_STREQ(std::getenv(SS_SDK_ENV_LOG_MIRROR_PATH), "logs/from_daemon.log");
}
