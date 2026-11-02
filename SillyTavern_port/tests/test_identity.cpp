#include "test_util.h"

#include "core/identity.h"
#include "core/settings.h"

#include <string>

TEST(identity_defaults_to_user) {
    CHECK_EQ(st::normalize_user_name(""), "User");
    CHECK_EQ(st::normalize_user_name("   "), "User");
}

TEST(identity_trims_and_collapses) {
    CHECK_EQ(st::normalize_user_name("  Alice  "), "Alice");
    CHECK_EQ(st::normalize_user_name("Alice   Smith"), "Alice Smith");
}

TEST(identity_removes_controls) {
    CHECK_EQ(st::normalize_user_name(std::string("A") + '\x01' + "B"), "AB");
    CHECK_EQ(st::normalize_user_name("A\tB\nC"), "ABC");
}

TEST(identity_caps_length) {
    CHECK(st::normalize_user_name(std::string(200, 'a')).size() <= 64);

    std::string multibyte;
    for (int i = 0; i < 100; ++i) {
        multibyte += "é";
    }
    const auto result = st::normalize_user_name(multibyte);
    CHECK(result.size() <= 64);
    CHECK_EQ(result.size() % 2, std::size_t(0));
}

TEST(identity_is_used_by_settings) {
    st::Settings settings;
    settings.user_name = "  Alice  ";
    settings.validate();
    CHECK_EQ(settings.user_name, "Alice");

    settings.user_name = "   ";
    settings.validate();
    CHECK_EQ(settings.user_name, "User");
}
