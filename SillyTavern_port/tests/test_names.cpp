#include "test_util.h"
#include "temp_dir.h"

#include "core/names.h"

#include <string>

TEST(sanitize_rejects_traversal) {
    CHECK_EQ(st::sanitize_filename("../etc/passwd"), "etc_passwd");
    CHECK_EQ(st::sanitize_filename(".."), "unnamed");
    CHECK_EQ(st::sanitize_filename("/absolute"), "absolute");
    CHECK_EQ(st::sanitize_filename("..\\..\\windows"), "windows");
}

TEST(sanitize_removes_control_characters) {
    CHECK_EQ(st::sanitize_filename(std::string("a") + '\x01' + "b\tc"), "abc");
    CHECK_EQ(st::sanitize_filename("  spaced name  "), "spaced name");
    CHECK_EQ(st::sanitize_filename("a__b"), "a_b");
}

TEST(sanitize_preserves_unicode) {
    CHECK_EQ(st::sanitize_filename("Séra—夜"), "Séra—夜");
}

TEST(sanitize_uses_fallback_for_empty) {
    CHECK_EQ(st::sanitize_filename(""), "unnamed");
    CHECK_EQ(st::sanitize_filename("...", "fallback"), "fallback");
    CHECK_EQ(st::sanitize_filename("___", "fallback"), "fallback");
}

TEST(sanitize_caps_length_without_splitting_utf8) {
    const std::string long_name(300, 'a');
    const auto result = st::sanitize_filename(long_name);
    CHECK(result.size() <= 200);
    CHECK(!result.empty());

    std::string multibyte;
    for (int i = 0; i < 150; ++i) {
        multibyte += "é";
    }
    const auto unicode_result = st::sanitize_filename(multibyte);
    CHECK(unicode_result.size() <= 200);
    CHECK_EQ(unicode_result.size() % 2, std::size_t(0));
}

TEST(safe_join_confines_paths) {
    TempDir temp;
    const auto joined = st::safe_join(temp.path(), "chats/Seraphina");
    CHECK_EQ(joined, temp.path() / "chats" / "Seraphina");

    bool threw = false;
    try {
        st::safe_join(temp.path(), "../escape");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    bool absolute_threw = false;
    try {
        st::safe_join(temp.path(), "/etc/passwd");
    } catch (const std::runtime_error&) {
        absolute_threw = true;
    }
    CHECK(absolute_threw);

    bool inner_threw = false;
    try {
        st::safe_join(temp.path(), "a/../../b");
    } catch (const std::runtime_error&) {
        inner_threw = true;
    }
    CHECK(inner_threw);
}
