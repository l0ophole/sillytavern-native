#include "test_util.h"
#include "temp_dir.h"

#include "core/atomic_file.h"
#include "core/credentials.h"

#include <sys/stat.h>

TEST(mask_secret_hides_middle) {
    CHECK_EQ(st::mask_secret(""), "");
    CHECK_EQ(st::mask_secret("short"), "****");
    CHECK_EQ(st::mask_secret("sk-1234567890"), "sk-...7890");
}

TEST(ensure_private_file_sets_0600) {
    TempDir temp;
    const auto file = temp.path() / "secret";
    st::write_file_atomic(file, "x", std::filesystem::perms::owner_read |
                                   std::filesystem::perms::group_read |
                                   std::filesystem::perms::others_read);
    st::ensure_private_file(file);
    struct stat info {};
    CHECK_EQ(::stat(file.c_str(), &info), 0);
    CHECK_EQ(static_cast<unsigned>(info.st_mode & 0777), 0600u);
}

TEST(is_secret_redacted_detects_presence) {
    CHECK(st::is_secret_redacted("nothing here", "sk-secret"));
    CHECK(!st::is_secret_redacted("key sk-secret", "sk-secret"));
    CHECK(st::is_secret_redacted("anything", ""));
}
