#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

extern "C" {
#include "config.h"
}

namespace {

class ScopedConfig {
public:
    ScopedConfig() : cfg_(config_create()) {}
    ~ScopedConfig() { config_destroy(&cfg_); }

    config *get() const { return cfg_; }

private:
    config *cfg_;
};

std::string write_temp_json_file(const char *content)
{
    char path_template[] = "/tmp/sunspots_config_test_XXXXXX";
    int fd = mkstemp(path_template);
    if (fd < 0) {
        return "";
    }

    const size_t len = std::strlen(content);
    const ssize_t nw = write(fd, content, len);
    close(fd);
    if (nw < 0 || static_cast<size_t>(nw) != len) {
        unlink(path_template);
        return "";
    }
    return std::string(path_template);
}

class ScopedEnvVar {
public:
    explicit ScopedEnvVar(const char *name) : name_(name), old_(), had_old_(false)
    {
        const char *existing = std::getenv(name_);
        if (existing != nullptr) {
            had_old_ = true;
            old_ = existing;
        }
    }

    ~ScopedEnvVar()
    {
        if (had_old_) {
            setenv(name_, old_.c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }

private:
    const char *name_;
    std::string old_;
    bool had_old_;
};

}  // namespace

TEST(config_module, loads_and_reads_file_values)
{
    ScopedConfig cfg;
    ASSERT_NE(cfg.get(), nullptr);

    const std::string path = write_temp_json_file(
        "{\"server\":{\"port\":8081,\"host\":\"127.0.0.1\"},\"debug\":true}");
    ASSERT_FALSE(path.empty());

    EXPECT_EQ(config_load_file(cfg.get(), path.c_str()), 0);
    unlink(path.c_str());

    int port = 0;
    EXPECT_EQ(config_get_int(cfg.get(), "server.port", &port), 0);
    EXPECT_EQ(port, 8081);

    bool debug = false;
    EXPECT_EQ(config_get_bool(cfg.get(), "debug", &debug), 0);
    EXPECT_TRUE(debug);

    char host[32] = {0};
    EXPECT_EQ(config_get_string(cfg.get(), "server.host", host, sizeof(host)), 0);
    EXPECT_STREQ(host, "127.0.0.1");
}

TEST(config_module, merges_file_and_arg_overrides)
{
    ScopedConfig cfg;
    ASSERT_NE(cfg.get(), nullptr);

    const std::string path = write_temp_json_file(
        "{\"server\":{\"port\":8080},\"debug\":false}");
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(config_load_file(cfg.get(), path.c_str()), 0);
    unlink(path.c_str());

    std::vector<const char *> args = {
        "config_module_test",
        "--server.port", "9090",
        "--debug", "true",
    };
    EXPECT_EQ(
        config_load_args(cfg.get(), static_cast<int>(args.size()),
                         const_cast<char **>(args.data())),
        0);

    int port = 0;
    bool debug = false;
    EXPECT_EQ(config_get_int(cfg.get(), "server.port", &port), 0);
    EXPECT_EQ(config_get_bool(cfg.get(), "debug", &debug), 0);
    EXPECT_EQ(port, 9090);
    EXPECT_TRUE(debug);
}

TEST(config_module, applies_environment_overrides)
{
    ScopedConfig cfg;
    ASSERT_NE(cfg.get(), nullptr);

    ScopedEnvVar port_guard("SUNSPOTS_PORT");
    ScopedEnvVar host_guard("SUNSPOTS_HOST");
    ScopedEnvVar debug_guard("SUNSPOTS_DEBUG");

    setenv("SUNSPOTS_PORT", "7001", 1);
    setenv("SUNSPOTS_HOST", "env.example.local", 1);
    setenv("SUNSPOTS_DEBUG", "1", 1);

    EXPECT_EQ(config_load_env(cfg.get()), 0);

    EXPECT_EQ(config_get_int_or(cfg.get(), "port", -1), 7001);
    EXPECT_STREQ(
        config_get_string_or(cfg.get(), "host", "missing"),
        "env.example.local");
    EXPECT_TRUE(config_get_bool_or(cfg.get(), "debug", false));
}

TEST(config_module, handles_missing_and_invalid_inputs)
{
    ScopedConfig cfg;
    ASSERT_NE(cfg.get(), nullptr);

    EXPECT_EQ(config_load_file(cfg.get(), "does-not-exist.json"), -ENOENT);

    const std::string invalid_path = write_temp_json_file("{not-json}");
    ASSERT_FALSE(invalid_path.empty());
    EXPECT_EQ(config_load_file(cfg.get(), invalid_path.c_str()), -EINVAL);
    unlink(invalid_path.c_str());

    int out_int = 0;
    EXPECT_EQ(config_get_int(cfg.get(), "missing.path", &out_int), -ENOENT);
    EXPECT_EQ(config_get_int_or(cfg.get(), "missing.path", 1234), 1234);
}

TEST(config_module, demonstrates_safe_default_accessors_with_null)
{
    EXPECT_EQ(config_get_int_or(nullptr, "any", 42), 42);
    EXPECT_FALSE(config_get_bool_or(nullptr, "any", false));
    EXPECT_STREQ(config_get_string_or(nullptr, "any", "fallback"), "fallback");
}
