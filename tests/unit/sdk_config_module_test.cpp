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

TEST_F(SdkConfigFixture, sdk_env_vars_must_be_exported_by_daemon)
{
    ASSERT_EQ(unsetenv("SUNSPOTS_CONFIG"), 0);
    clear_sdk_env();

    ASSERT_NE(std::getenv(SS_SDK_ENV_DB_PATH), nullptr);
    ASSERT_NE(std::getenv(SS_SDK_ENV_LOG_LEVEL), nullptr);
    ASSERT_NE(std::getenv(SS_SDK_ENV_LOG_MIRROR_ENABLED), nullptr);
    ASSERT_NE(std::getenv(SS_SDK_ENV_LOG_MIRROR_PATH), nullptr);
}
