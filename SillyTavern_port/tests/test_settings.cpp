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

TEST(settings_authors_note_round_trips_and_clamps) {
    TempDir temp;
    const auto file = temp.path() / "settings.json";
    auto settings = st::Settings::load(file);
    CHECK(!settings.authors_note_enabled);
    CHECK(settings.authors_note.empty());
    CHECK_EQ(settings.authors_note_depth, 4);
    settings.authors_note_enabled = true;
    settings.authors_note = "Remember {{char}} is brave.";
    settings.authors_note_depth = 7;
    settings.save(file);
    const auto loaded = st::Settings::load(file);
    CHECK(loaded.authors_note_enabled);
    CHECK_EQ(loaded.authors_note, "Remember {{char}} is brave.");
    CHECK_EQ(loaded.authors_note_depth, 7);

    st::Settings clamped;
    clamped.authors_note_depth = 500;
    clamped.authors_note = std::string(5000, 'x');
    clamped.validate();
    CHECK_EQ(clamped.authors_note_depth, 100);
    CHECK_EQ(clamped.authors_note.size(), std::size_t(4096));

    st::Settings negative;
    negative.authors_note_depth = -10;
    negative.validate();
    CHECK_EQ(negative.authors_note_depth, 0);
}

TEST(settings_world_info_round_trips) {
    TempDir temp;
    const auto file = temp.path() / "settings.json";
    auto settings = st::Settings::load(file);
    CHECK(!settings.world_info_enabled);
    CHECK_EQ(settings.world_info_book, "lorebook");
    settings.world_info_enabled = true;
    settings.world_info_book = "dragons";
    settings.save(file);
    const auto loaded = st::Settings::load(file);
    CHECK(loaded.world_info_enabled);
    CHECK_EQ(loaded.world_info_book, "dragons");

    st::Settings sanitized;
    sanitized.world_info_book = "../evil/book";
    sanitized.validate();
    CHECK(sanitized.world_info_book.find('/') == std::string::npos);
    CHECK(sanitized.world_info_book.find("..") == std::string::npos);
    st::Settings empty;
    empty.world_info_book = "";
    empty.validate();
    CHECK_EQ(empty.world_info_book, "lorebook");
}

TEST(settings_instruct_defaults_unchanged) {
    TempDir temp;
    const auto settings = st::Settings::load(temp.path() / "settings.json");
    CHECK(!settings.instruct_mode);
    CHECK_EQ(settings.instruct_format, "plain");
    CHECK(settings.context_template.empty());

    const auto file = temp.path() / "settings.json";
    auto round = st::Settings::load(file);
    round.instruct_mode = true;
    round.instruct_format = "chatml";
    round.context_template = "Template {{char}}";
    round.save(file);
    const auto loaded = st::Settings::load(file);
    CHECK(loaded.instruct_mode);
    CHECK_EQ(loaded.instruct_format, "chatml");
    CHECK_EQ(loaded.context_template, "Template {{char}}");
}

TEST(instruct_unknown_format_clamps_to_plain) {
    st::Settings settings;
    settings.instruct_format = "bogus";
    settings.context_template = std::string(9000, 'x');
    settings.validate();
    CHECK_EQ(settings.instruct_format, "plain");

    st::Settings chatml;
    chatml.instruct_format = "chatml";
    chatml.validate();
    CHECK_EQ(chatml.instruct_format, "chatml");

    st::Settings llama;
    llama.instruct_format = "llama";
    llama.validate();
    CHECK_EQ(llama.instruct_format, "llama");

    CHECK_EQ(settings.context_template.size(), std::size_t(8192));
}

TEST(settings_regex_and_variables_round_trip) {
    TempDir temp;
    const auto file = temp.path() / "settings.json";
    auto settings = st::Settings::load(file);
    settings.top_k = 40;
    settings.repetition_penalty = 1.1;
    settings.presence_penalty = 0.5;
    settings.frequency_penalty = -0.5;
    settings.dry_multiplier = 0.8;
    settings.mirostat_tau = 6.0;
    settings.regex_rules = st::Json::array({st::Json{{"pattern", "foo"},
                                                     {"replacement", "bar"},
                                                     {"applies_to_user", true},
                                                     {"applies_to_assistant", false}}});
    settings.variables = {{"mood", "calm"}};
    settings.save(file);
    const auto loaded = st::Settings::load(file);
    CHECK_EQ(loaded.top_k, 40);
    CHECK_EQ(loaded.repetition_penalty, 1.1);
    CHECK_EQ(loaded.presence_penalty, 0.5);
    CHECK_EQ(loaded.frequency_penalty, -0.5);
    CHECK_EQ(loaded.dry_multiplier, 0.8);
    CHECK_EQ(loaded.mirostat_tau, 6.0);
    CHECK_EQ(loaded.regex_rules.dump(), settings.regex_rules.dump());
    CHECK_EQ(loaded.variables.size(), std::size_t(1));
    CHECK_EQ(loaded.variables.at("mood"), "calm");

    st::Settings clamped;
    clamped.top_k = 5000;
    clamped.repetition_penalty = 99.0;
    clamped.presence_penalty = 99.0;
    clamped.frequency_penalty = -99.0;
    clamped.dry_multiplier = 99.0;
    clamped.mirostat_tau = 99.0;
    clamped.validate();
    CHECK_EQ(clamped.top_k, 1000);
    CHECK_EQ(clamped.repetition_penalty, 2.0);
    CHECK_EQ(clamped.presence_penalty, 2.0);
    CHECK_EQ(clamped.frequency_penalty, -2.0);
    CHECK_EQ(clamped.dry_multiplier, 10.0);
    CHECK_EQ(clamped.mirostat_tau, 20.0);
}

TEST(settings_persona_name_round_trips_and_truncates) {
    TempDir temp;
    const auto file = temp.path() / "settings.json";
    auto settings = st::Settings::load(file);
    CHECK(settings.persona_name.empty());
    settings.persona_name = "Mia";
    settings.save(file);
    const auto loaded = st::Settings::load(file);
    CHECK_EQ(loaded.persona_name, "Mia");

    st::Settings long_name;
    long_name.persona_name = std::string(300, 'x');
    long_name.validate();
    CHECK_EQ(long_name.persona_name.size(), std::size_t(256));

    st::Settings padded;
    padded.persona_name = "  Mia  ";
    padded.validate();
    CHECK_EQ(padded.persona_name, "Mia");
}
