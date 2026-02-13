#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <limits.h>
#include <unistd.h>

extern "C" {
#define ATOMIC_FILE_RW_IMPLEMENTATION
#include "atomic_file_rw.h"
}

namespace {

std::string make_temp_dir()
{
    char tpl[] = "/tmp/sunspots_atomic_rw_test_XXXXXX";
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
            (void)chdir(old_.c_str());
        }
    }

    bool ok() const { return ok_; }

private:
    bool ok_;
    std::string old_;
};

}  // namespace

TEST(atomic_file_rw, af_save_creates_default_parent_directory)
{
    const std::string dir = make_temp_dir();
    ASSERT_FALSE(dir.empty());

    ScopedCwd cwd(dir);
    ASSERT_TRUE(cwd.ok());

    ASSERT_EQ(access(".db", F_OK), -1);
    ASSERT_EQ(af_save("sdk", "event", "payload"), 0);
    ASSERT_EQ(access(".db", F_OK), 0);
    ASSERT_EQ(access(".db/database.jsonl", F_OK), 0);

    remove_file_if_exists(dir + "/.db/database.jsonl");
    remove_dir_if_exists(dir + "/.db");
    remove_dir_if_exists(dir);
}

TEST(atomic_file_rw, af_read_returns_appended_json_lines)
{
    const std::string dir = make_temp_dir();
    ASSERT_FALSE(dir.empty());

    ScopedCwd cwd(dir);
    ASSERT_TRUE(cwd.ok());

    ASSERT_EQ(af_save("source_a", "type_a", "hello"), 0);
    ASSERT_EQ(af_save("source_b", "type_b", "world"), 0);

    size_t size = 0;
    char *body = af_read(&size);
    ASSERT_NE(body, (char *)NULL);
    ASSERT_GT(size, (size_t)0);

    const std::string text(body);
    free(body);

    EXPECT_NE(text.find("source_a"), std::string::npos);
    EXPECT_NE(text.find("source_b"), std::string::npos);
    EXPECT_NE(text.find("\n"), std::string::npos);

    remove_file_if_exists(dir + "/.db/database.jsonl");
    remove_dir_if_exists(dir + "/.db");
    remove_dir_if_exists(dir);
}
