#include "test_util.h"
#include "temp_dir.h"

#include "core/atomic_file.h"

#include <sys/stat.h>

TEST(atomic_write_read_roundtrip) {
    TempDir temp;
    const auto file = temp.path() / "nested" / "file.txt";
    st::write_file_atomic(file, "hello");
    CHECK_EQ(st::read_file(file), "hello");
}

TEST(atomic_write_replaces_existing) {
    TempDir temp;
    const auto file = temp.path() / "file.txt";
    st::write_file_atomic(file, "old");
    st::write_file_atomic(file, "new");
    CHECK_EQ(st::read_file(file), "new");
}

TEST(atomic_write_sets_requested_mode) {
    TempDir temp;
    const auto file = temp.path() / "secret.json";
    st::write_file_atomic(file, "{}", std::filesystem::perms::owner_read |
                                      std::filesystem::perms::owner_write);
    struct stat info {};
    CHECK_EQ(::stat(file.c_str(), &info), 0);
    CHECK_EQ(static_cast<unsigned>(info.st_mode & 0777), 0600u);
}

TEST(read_missing_file_throws) {
    TempDir temp;
    bool threw = false;
    try {
        st::read_file(temp.path() / "missing");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(read_file_limited_enforces_cap) {
    TempDir temp;
    const auto file = temp.path() / "data.txt";
    st::write_file_atomic(file, "0123456789");
    CHECK_EQ(st::read_file_limited(file, 10, "test"), "0123456789");

    bool threw = false;
    try {
        (void)st::read_file_limited(file, 9, "test");
    } catch (const std::runtime_error& error) {
        threw = true;
        const std::string message = error.what();
        CHECK(message.find("test") != std::string::npos);
        CHECK(message.find("too large") != std::string::npos);
    }
    CHECK(threw);
}
