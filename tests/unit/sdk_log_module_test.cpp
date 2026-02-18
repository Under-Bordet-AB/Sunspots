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
#include "sdk/internal/log/ss_log_internal.h"
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

class ScopedCwd {
public:
    explicit ScopedCwd(const std::string &next) : ok_(false)
    {
        char buf[4096];
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
            (void)chdir(old_.c_str());
        }
    }

    bool ok() const { return ok_; }

private:
    bool ok_;
    std::string old_;
};

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

class SdkLogFixture : public ::testing::Test {
protected:
    SdkLogFixture()
        : cfg_guard_("SUNSPOTS_CONFIG"),
          mirror_enabled_guard_("SS_SDK_LOG_MIRROR_ENABLED"),
          mirror_path_guard_("SS_SDK_LOG_MIRROR_PATH"),
          db_path_guard_("SS_SDK_DB_PATH")
    {}

    void SetUp() override
    {
        dir_ = make_temp_dir();
        ASSERT_FALSE(dir_.empty());
    }

    void TearDown() override
    {
        ss_sdk_shutdown();
        remove_file_if_exists(log_path_);
        remove_dir_if_exists(nested_dir_);
        remove_dir_if_exists(dir_);
    }

    void use_log_path(const std::string &relative)
    {
        nested_dir_ = dir_ + "/nested";
        log_path_ = nested_dir_ + "/" + relative;
        ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "1", 1), 0);
        ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_PATH", log_path_.c_str(), 1), 0);
    }

    ScopedEnvVar cfg_guard_;
    ScopedEnvVar mirror_enabled_guard_;
    ScopedEnvVar mirror_path_guard_;
    ScopedEnvVar db_path_guard_;
    std::string dir_;
    std::string nested_dir_;
    std::string log_path_;
};

}  // namespace

TEST_F(SdkLogFixture, invalid_level_returns_invalid_arg)
{
    EXPECT_EQ(
        ss_sdk_log_write_auto((ss_sdk_log_level)99, "sdk.log.bad.level", "message", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INVALID_ARG);
}

TEST_F(SdkLogFixture, invalid_base_arguments_return_invalid_arg)
{
    EXPECT_EQ(ss_sdk_log_write_auto(SS_SDK_LOG_INFO, NULL, "m", __FILE__, __LINE__, __func__), SS_SDK_OK);
    EXPECT_EQ(ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "", "m", __FILE__, __LINE__, __func__), SS_SDK_OK);
    EXPECT_EQ(ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "e", NULL, __FILE__, __LINE__, __func__), SS_SDK_ERR_INVALID_ARG);
    EXPECT_EQ(ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "e", "m", NULL, __LINE__, __func__), SS_SDK_ERR_INVALID_ARG);
    EXPECT_EQ(ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "e", "m", __FILE__, __LINE__, NULL), SS_SDK_ERR_INVALID_ARG);
}

TEST_F(SdkLogFixture, null_or_empty_event_uses_placeholder_and_writes_log)
{
    use_log_path("optional_event.log");
    ASSERT_EQ(ss_sdk_log_write_auto(SS_SDK_LOG_INFO, NULL, "from_null_event", __FILE__, __LINE__, __func__), SS_SDK_OK);
    ASSERT_EQ(ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "", "from_empty_event", __FILE__, __LINE__, __func__), SS_SDK_OK);

    const std::string text = read_text_file(log_path_);
    EXPECT_NE(text.find(" INFO - "), std::string::npos);
    EXPECT_NE(text.find("msg=\"from_null_event\""), std::string::npos);
    EXPECT_NE(text.find("msg=\"from_empty_event\""), std::string::npos);
}

TEST_F(SdkLogFixture, mirror_enabled_without_path_uses_default_path)
{
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "1", 1), 0);
    ASSERT_EQ(unsetenv("SS_SDK_LOG_MIRROR_PATH"), 0);

    char out_path[256];
    ASSERT_EQ(ss_sdk_internal_log_test_get_log_path(out_path, sizeof(out_path)), 0);
    EXPECT_STREQ(out_path, "logs/sdk.log");
}

TEST_F(SdkLogFixture, missing_log_path_is_intentional_noop)
{
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "0", 1), 0);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.noop", "message", __FILE__, __LINE__, __func__),
        SS_SDK_OK);
}

TEST_F(SdkLogFixture, mirror_path_env_is_used)
{
    use_log_path("sdk log.txt");

    ASSERT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.good_path", "hello", __FILE__, __LINE__, __func__),
        SS_SDK_OK);

    const std::string text = read_text_file(log_path_);
    EXPECT_NE(text.find("sdk.log.good_path"), std::string::npos);
}

TEST_F(SdkLogFixture, overlong_log_path_returns_internal_error)
{
    const std::string long_path(1500, 'a');
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "1", 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_PATH", long_path.c_str(), 1), 0);

    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.long_path", "message", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);
}

TEST_F(SdkLogFixture, mirror_disabled_ignores_path_and_returns_ok)
{
    nested_dir_ = dir_ + "/nested";
    const std::string ignored_path = nested_dir_ + "/ignored.log";
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "0", 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_PATH", ignored_path.c_str(), 1), 0);

    ASSERT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.mirror_disabled", "message", __FILE__, __LINE__, __func__),
        SS_SDK_OK);

    EXPECT_EQ(access(ignored_path.c_str(), F_OK), -1);
}

TEST_F(SdkLogFixture, write_fields_includes_structured_fields)
{
    use_log_path("fields.log");

    ss_sdk_log_fields fields;
    std::memset(&fields, 0, sizeof(fields));
    fields.module = "fetch.openmeteo";
    fields.source_api = "openmeteo";
    fields.metric = SS_METRIC_WEATHER_WIND_SPEED_10M_MS;
    fields.ts_utc = 1735689600;

    ASSERT_EQ(
        ss_sdk_log_write_fields(
            SS_SDK_LOG_WARN,
            "sdk.log.fields",
            "structured",
            &fields,
            __FILE__,
            __LINE__,
            __func__),
        SS_SDK_OK);

    const std::string text = read_text_file(log_path_);
    EXPECT_NE(text.find("sdk.log.fields"), std::string::npos);
    EXPECT_NE(text.find("module=fetch.openmeteo"), std::string::npos);
    EXPECT_NE(text.find("source_api=openmeteo"), std::string::npos);
    EXPECT_NE(text.find("metric=" + std::to_string((int)SS_METRIC_WEATHER_WIND_SPEED_10M_MS)), std::string::npos);
    EXPECT_NE(text.find("ts_utc=1735689600"), std::string::npos);
}

TEST_F(SdkLogFixture, write_fields_escapes_quotes_backslashes_and_control_chars)
{
    use_log_path("escape.log");

    ss_sdk_log_fields fields;
    std::memset(&fields, 0, sizeof(fields));
    fields.module = "module\tname";
    fields.source_api = "provider\"x\\y";

    ASSERT_EQ(
        ss_sdk_log_write_fields(
            SS_SDK_LOG_ERROR,
            "sdk.log.\"quoted\"",
            "line1\nline2\tend",
            &fields,
            __FILE__,
            __LINE__,
            __func__),
        SS_SDK_OK);

    const std::string text = read_text_file(log_path_);
    EXPECT_NE(text.find("sdk.log.\\\"quoted\\\""), std::string::npos);
    EXPECT_NE(text.find("msg=\"line1\\nline2\\tend\""), std::string::npos);
    EXPECT_NE(text.find("module=module\\tname"), std::string::npos);
    EXPECT_NE(text.find("source_api=provider\\\"x\\\\y"), std::string::npos);
}

TEST_F(SdkLogFixture, macro_logging_writes_info_level_event)
{
    use_log_path("macro.log");

    ASSERT_EQ(SS_LOG_INFO("sdk.log.macro", "from_macro"), SS_SDK_OK);

    const std::string text = read_text_file(log_path_);
    EXPECT_NE(text.find(" INFO sdk.log.macro "), std::string::npos);
    EXPECT_NE(text.find("msg=\"from_macro\""), std::string::npos);
}

#ifdef SS_SDK_ENABLE_TEST_HOOKS
TEST_F(SdkLogFixture, internal_test_helpers_cover_low_level_branches)
{
    int saved_errno = 0;
    EXPECT_EQ(ss_sdk_internal_log_test_checked_add_overflow(&saved_errno), -1);
    EXPECT_EQ(saved_errno, EOVERFLOW);

    EXPECT_EQ(ss_sdk_internal_log_test_strdup_local(NULL), (char *)NULL);

    EXPECT_EQ(ss_sdk_internal_log_test_ensure_parent_dirs(NULL), -1);
    EXPECT_EQ(ss_sdk_internal_log_test_level_to_string((ss_sdk_log_level)99), (const char *)NULL);

    char out_path[8];
    EXPECT_EQ(ss_sdk_internal_log_test_extract_json_log_path(NULL, out_path, sizeof(out_path)), -1);
    EXPECT_EQ(ss_sdk_internal_log_test_extract_json_log_path("{\"log_path\":123}", out_path, sizeof(out_path)), -1);
    EXPECT_EQ(ss_sdk_internal_log_test_extract_json_log_path("{\"log_path\":\"\"}", out_path, sizeof(out_path)), 0);
    EXPECT_STREQ(out_path, "");
}

TEST_F(SdkLogFixture, internal_helpers_cover_escape_and_write_error_paths)
{
    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_CHECKED_ADD, 1);
    EXPECT_EQ(ss_sdk_internal_log_test_escape_text("x"), (char *)NULL);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_STRDUP, 1);
    EXPECT_EQ(ss_sdk_internal_log_test_ensure_parent_dirs("a/b"), -1);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_MKDIR, 1);
    EXPECT_EQ(ss_sdk_internal_log_test_ensure_parent_dirs("a/b"), -1);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FORCE_WRITE_ZERO, 1);
    EXPECT_EQ(ss_sdk_internal_log_test_write_all(STDOUT_FILENO, "abc", 3), -1);
    EXPECT_EQ(errno, EIO);
}

TEST_F(SdkLogFixture, write_auto_hooked_failures_return_internal)
{
    use_log_path("hooked.log");

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_GMTIME, 1);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.hook.gmtime", "m", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_STRFTIME, 1);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.hook.strftime", "m", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_ESCAPE_BASE, 1);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.hook.escape_base", "m", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_FORMAT_LINE, 1);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.hook.format_line", "m", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_FSYNC, 1);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.hook.fsync", "m", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_ESCAPE_OPTIONAL, 1);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.hook.escape_optional_auto", "m", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);
}

TEST_F(SdkLogFixture, write_fields_hooked_optional_escape_failure_returns_internal)
{
    use_log_path("hooked_fields.log");

    ss_sdk_log_fields fields;
    std::memset(&fields, 0, sizeof(fields));
    fields.module = "m";
    fields.source_api = "s";
    fields.metric = SS_METRIC_WEATHER_WIND_SPEED_10M_MS;
    fields.ts_utc = 1;

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_ESCAPE_OPTIONAL, 1);
    EXPECT_EQ(
        ss_sdk_log_write_fields(
            SS_SDK_LOG_INFO,
            "sdk.log.hook.escape_optional",
            "m",
            &fields,
            __FILE__,
            __LINE__,
            __func__),
        SS_SDK_ERR_INTERNAL);
}

TEST_F(SdkLogFixture, get_log_path_helper_handles_missing_env)
{
    ASSERT_EQ(unsetenv("SS_SDK_LOG_MIRROR_ENABLED"), 0);
    ASSERT_EQ(unsetenv("SS_SDK_LOG_MIRROR_PATH"), 0);
    char out_path[64];
    ASSERT_EQ(ss_sdk_internal_log_test_get_log_path(out_path, sizeof(out_path)), 0);
    EXPECT_STREQ(out_path, "logs/sdk.log");
}

TEST_F(SdkLogFixture, missing_env_defaults_write_to_logs_sdk_log)
{
    const std::string default_log_path = dir_ + "/logs/sdk.log";
    ScopedCwd cwd(dir_);
    ASSERT_TRUE(cwd.ok());
    ASSERT_EQ(unsetenv("SS_SDK_LOG_MIRROR_ENABLED"), 0);
    ASSERT_EQ(unsetenv("SS_SDK_LOG_MIRROR_PATH"), 0);

    ASSERT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.default.path", "default-path-write", __FILE__, __LINE__, __func__),
        SS_SDK_OK);

    const std::string text = read_text_file(default_log_path);
    EXPECT_NE(text.find("sdk.log.default.path"), std::string::npos);
    EXPECT_NE(text.find("msg=\"default-path-write\""), std::string::npos);
}

TEST_F(SdkLogFixture, invalid_mirror_enabled_token_disables_mirror)
{
    nested_dir_ = dir_ + "/nested";
    const std::string ignored_path = nested_dir_ + "/ignored.log";
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "maybe", 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_PATH", ignored_path.c_str(), 1), 0);

    ASSERT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.invalid.toggle", "message", __FILE__, __LINE__, __func__),
        SS_SDK_OK);
    EXPECT_EQ(access(ignored_path.c_str(), F_OK), -1);
}

TEST_F(SdkLogFixture, internal_helpers_cover_remaining_low_level_branches)
{
    char ts[32];

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook((ss_sdk_log_test_hook)999, 1);
    EXPECT_EQ(ss_sdk_internal_log_test_consume_null_slot(), 0);

    const std::string parent_file = dir_ + "/parent_is_file";
    FILE *fp = std::fopen(parent_file.c_str(), "w");
    ASSERT_NE(fp, nullptr);
    std::fclose(fp);
    EXPECT_EQ(ss_sdk_internal_log_test_ensure_parent_dirs((parent_file + "/child/log.txt").c_str()), -1);
    remove_file_if_exists(parent_file);

    char *escaped = ss_sdk_internal_log_test_escape_text(NULL);
    ASSERT_NE(escaped, (char *)NULL);
    EXPECT_STREQ(escaped, "");
    free(escaped);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_CHECKED_ADD, 1);
    EXPECT_EQ(ss_sdk_internal_log_test_escape_text("\""), (char *)NULL);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_CHECKED_ADD, 1);
    EXPECT_EQ(ss_sdk_internal_log_test_escape_text(""), (char *)NULL);

    char out_path[64];
    EXPECT_EQ(ss_sdk_internal_log_test_extract_json_log_path("{", out_path, sizeof(out_path)), -1);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_GMTIME, 1);
    EXPECT_EQ(ss_sdk_internal_log_test_format_utc_timestamp(ts), -1);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_STRFTIME, 1);
    EXPECT_EQ(ss_sdk_internal_log_test_format_utc_timestamp(ts), -1);

    ss_sdk_internal_log_test_escaped_fields_free_null();
}

TEST_F(SdkLogFixture, write_all_handles_eintr_and_non_eintr_errors)
{
    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FORCE_WRITE_EINTR, 1);
    EXPECT_EQ(ss_sdk_internal_log_test_write_all(STDOUT_FILENO, "abc", 3), 0);

    ss_sdk_internal_log_test_reset_hooks();
    EXPECT_EQ(ss_sdk_internal_log_test_write_all(-1, "abc", 3), -1);
}

TEST_F(SdkLogFixture, write_auto_covers_path_open_flock_write_and_fsync_call_errors)
{
    use_log_path("error_paths.log");

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_MKDIR, 1);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.hook.mkdir", "m", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);

    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_ENABLED", "1", 1), 0);
    ASSERT_EQ(setenv("SS_SDK_LOG_MIRROR_PATH", "/", 1), 0);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.root.open_fail", "m", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);

    use_log_path("error_paths.log");
    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_FLOCK, 1);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.hook.flock", "m", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FORCE_WRITE_ZERO, 1);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.hook.write_zero", "m", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_FSYNC_CALL, 1);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.hook.fsync_call", "m", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);
}

TEST_F(SdkLogFixture, format_and_escape_alloc_failures_cover_common_error_paths)
{
    use_log_path("format_escape_fail.log");

    ss_sdk_log_fields fields;
    std::memset(&fields, 0, sizeof(fields));
    fields.module = "m";
    fields.source_api = "s";
    fields.metric = SS_METRIC_WEATHER_WIND_SPEED_10M_MS;
    fields.ts_utc = 1;

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_ESCAPE_ALLOC, 1);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.hook.escape_alloc_base", "m", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FAIL_ESCAPE_ALLOC, 5);
    EXPECT_EQ(
        ss_sdk_log_write_fields(
            SS_SDK_LOG_INFO,
            "sdk.log.hook.escape_alloc_optional",
            "m",
            &fields,
            __FILE__,
            __LINE__,
            __func__),
        SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FORCE_FORMAT_NEEDED_NEG, 1);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.hook.needed_neg", "m", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);

    ss_sdk_internal_log_test_reset_hooks();
    ss_sdk_internal_log_test_set_hook(SS_SDK_LOG_HOOK_FORCE_FORMAT_ALLOC_NULL, 1);
    EXPECT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_INFO, "sdk.log.hook.alloc_null", "m", __FILE__, __LINE__, __func__),
        SS_SDK_ERR_INTERNAL);
}

TEST_F(SdkLogFixture, debug_level_is_written)
{
    use_log_path("debug.log");
    ASSERT_EQ(
        ss_sdk_log_write_auto(SS_SDK_LOG_DEBUG, "sdk.log.debug", "hello", __FILE__, __LINE__, __func__),
        SS_SDK_OK);

    const std::string text = read_text_file(log_path_);
    EXPECT_NE(text.find(" DEBUG sdk.log.debug "), std::string::npos);
}
#endif
