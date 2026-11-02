#include "test_util.h"
#include "temp_dir.h"

#include "core/atomic_file.h"
#include "core/settings.h"

#include <sys/stat.h>

TEST(settings_defaults_when_missing) {
    TempDir temp;
    const auto settings = st::Settings::load(temp.path() / "settings.json");
    CHECK_EQ(settings.model, "gpt-4o-mini");
    CHECK_EQ(settings.max_tokens, 300);
    CHECK_EQ(settings.context_size, 4095);
    CHECK_EQ(settings.user_name, "User");
    CHECK(settings.stream);
}

TEST(settings_roundtrip_preserves_unknown_keys) {
    TempDir temp;
    const auto file = temp.path() / "settings.json";
    auto settings = st::Settings::load(file);
    settings.api_key = "sk-test";
    settings.unknown["future_flag"] = true;
    settings.save(file);
    const auto loaded = st::Settings::load(file);
    CHECK_EQ(loaded.api_key, "sk-test");
    CHECK_EQ(loaded.unknown["future_flag"].get<bool>(), true);
}

TEST(settings_validate_clamps) {
    st::Settings settings;
    settings.temperature = 9.0;
    settings.top_p = -1.0;
    settings.max_tokens = 999999;
    settings.context_size = 128;
    settings.request_timeout_seconds = 1;
    settings.validate();
    CHECK_EQ(settings.temperature, 2.0);
    CHECK_EQ(settings.top_p, 0.0);
    CHECK_EQ(settings.context_size, 256);
    CHECK(settings.max_tokens >= 1);
    CHECK(settings.max_tokens < settings.context_size);
    CHECK_EQ(settings.request_timeout_seconds, 5);
}

TEST(settings_save_is_private) {
    TempDir temp;
    const auto file = temp.path() / "settings.json";
    st::Settings{}.save(file);
    struct stat info {};
    CHECK_EQ(::stat(file.c_str(), &info), 0);
    CHECK_EQ(static_cast<unsigned>(info.st_mode & 0777), 0600u);
}

TEST(settings_load_repairs_permissions) {
    TempDir temp;
    const auto file = temp.path() / "settings.json";
    st::write_file_atomic(file, R"({"model":"private"})");
    CHECK_EQ(::chmod(file.c_str(), 0644), 0);
    const auto settings = st::Settings::load(file);
    CHECK_EQ(settings.model, "private");
    struct stat info {};
    CHECK_EQ(::stat(file.c_str(), &info), 0);
    CHECK_EQ(static_cast<unsigned>(info.st_mode & 0777), 0600u);
}

TEST(settings_load_rejects_oversized_file) {
    TempDir temp;
    const auto file = temp.path() / "settings.json";
    st::write_file_atomic(file, std::string(1024u * 1024u + 1u, 'x'));
    bool threw = false;
    try {
        (void)st::Settings::load(file);
    } catch (const std::runtime_error& error) {
        threw = true;
        const std::string message = error.what();
        CHECK(message.find("settings") != std::string::npos);
        CHECK(message.find("too large") != std::string::npos);
    }
    CHECK(threw);
}
