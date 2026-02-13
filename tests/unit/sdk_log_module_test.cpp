#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
    char tpl[] = "/tmp/sunspots_sdk_log_test_XXXXXX";
    char *dir = mkdtemp(tpl);
    if (dir == NULL) {
        return "";
    }
    return std::string(dir);
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

void remove_file_if_exists(const std::string &path)
{
    (void)unlink(path.c_str());
}

void remove_dir_if_exists(const std::string &path)
{
    (void)rmdir(path.c_str());
}

std::string json_escape(const std::string &in, bool escape_slash)
{
    std::string out;
    out.reserve(in.size() * 2 + 8);
    for (size_t i = 0; i < in.size(); ++i) {
        const char ch = in[i];
        if (ch == '"') {
            out += "\\\"";
        } else if (ch == '\\') {
            out += "\\\\";
        } else if (escape_slash && ch == '/') {
            out += "\\/";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

}  // namespace

TEST(sdk_log_module, malformed_config_returns_internal_error)
{
    ScopedEnvVar cfg_guard("SUNSPOTS_CONFIG");

    setenv("SUNSPOTS_CONFIG", "{\"log_path\":123}", 1);

    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.test", "message", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);
}

TEST(sdk_log_module, missing_log_path_is_intentional_noop)
{
    ScopedEnvVar cfg_guard("SUNSPOTS_CONFIG");

    setenv("SUNSPOTS_CONFIG", "{\"not_log_path\":\"x\"}", 1);

    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.test", "message", __FILE__, __LINE__, __func__),
        SS_SDK_OK);
}

TEST(sdk_log_module, escaped_json_log_path_is_decoded_and_used)
{
    ScopedEnvVar cfg_guard("SUNSPOTS_CONFIG");

    const std::string dir = make_temp_dir();
    ASSERT_FALSE(dir.empty());
    const std::string nested = dir + "/nested";
    const std::string log_path = nested + "/sdk log.txt";

    const std::string cfg =
        std::string("{\"log_path\":\"") + json_escape(log_path, true) + "\"}";
    setenv("SUNSPOTS_CONFIG", cfg.c_str(), 1);

    ASSERT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.good_path", "hello", __FILE__, __LINE__, __func__),
        SS_SDK_OK);

    const std::string text = read_text_file(log_path);
    EXPECT_NE(text.find("sdk.log.good_path"), std::string::npos);

    remove_file_if_exists(log_path);
    remove_dir_if_exists(nested);
    remove_dir_if_exists(dir);
}

TEST(sdk_log_module, overlong_log_path_returns_internal_error)
{
    ScopedEnvVar cfg_guard("SUNSPOTS_CONFIG");

    const std::string long_path(1500, 'a');
    const std::string cfg = std::string("{\"log_path\":\"") + long_path + "\"}";
    setenv("SUNSPOTS_CONFIG", cfg.c_str(), 1);

    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.long_path", "message", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);
}

TEST(sdk_log_module, ignores_log_path_tokens_inside_unrelated_string_values)
{
    ScopedEnvVar cfg_guard("SUNSPOTS_CONFIG");

    const std::string dir = make_temp_dir();
    ASSERT_FALSE(dir.empty());
    const std::string nested = dir + "/nested";
    const std::string good_path = nested + "/good.log";
    const std::string bad_path = dir + "/bad.log";

    const std::string noise =
        std::string("prefix \\\"log_path\\\":\\\"") + bad_path + "\\\" suffix";
    const std::string cfg =
        std::string("{\"noise\":\"") + json_escape(noise, true) + "\",\"log_path\":\"" +
        json_escape(good_path, true) + "\"}";
    setenv("SUNSPOTS_CONFIG", cfg.c_str(), 1);

    ASSERT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.good_key", "message", __FILE__, __LINE__, __func__),
        SS_SDK_OK);

    EXPECT_EQ(access(bad_path.c_str(), F_OK), -1);
    EXPECT_EQ(access(good_path.c_str(), F_OK), 0);

    remove_file_if_exists(good_path);
    remove_dir_if_exists(nested);
    remove_dir_if_exists(dir);
}
