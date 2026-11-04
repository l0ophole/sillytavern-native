#include "test_util.h"

#include "core/character_card.h"
#include "core/chat_types.h"
#include "core/prompt.h"
#include "core/settings.h"

#include <string>

TEST(prompt_expands_core_macros) {
    CHECK_EQ(st::expand_macros("{{char}} and <USER>", "Seraphina", "Alice"),
             "Seraphina and Alice");
    CHECK_EQ(st::expand_macros("{{CHAR}} <bot> {{User}} <user>", "S", "A"), "S S A A");
    CHECK_EQ(st::expand_macros("no macros", "S", "A"), "no macros");
}

TEST(prompt_does_not_expand_inserted_macros) {
    CHECK_EQ(st::expand_macros("{{char}}", "<USER>", "Alice"), "<USER>");
}

TEST(prompt_builds_system_sections_in_order) {
    st::CharacterCard card;
    card.name = "Seraphina";
    card.description = "A dragon.";
    card.personality = "Calm";
    card.scenario = "A cave";
    card.mes_example = "{{user}}: Hi\n{{char}}: Hello.";
    st::Settings settings;
    settings.user_name = "Alice";
    settings.system_prompt = "Global prompt.";

    const auto messages = st::build_prompt(card, settings, {});
    CHECK_EQ(messages.size(), std::size_t(1));
    CHECK_EQ(messages[0].role, "system");
    const std::string& content = messages[0].content;
    CHECK(content.find("Global prompt.") != std::string::npos);
    CHECK(content.find("A dragon.") != std::string::npos);
    CHECK(content.find("Seraphina's personality: Calm") != std::string::npos);
    CHECK(content.find("Scenario: A cave") != std::string::npos);
    CHECK(content.find("Example dialogues:\nAlice: Hi\nSeraphina: Hello.") != std::string::npos);
    CHECK(content.find("Global prompt.") < content.find("A dragon."));
    CHECK(content.find("A dragon.") < content.find("Seraphina's personality"));
    CHECK(content.find("Seraphina's personality") < content.find("Scenario: A cave"));
    CHECK(content.find("Scenario: A cave") < content.find("Example dialogues:"));
}

TEST(prompt_prefers_character_system_prompt) {
    st::CharacterCard card;
    card.name = "Seraphina";
    card.system_prompt = "Character prompt.";
    st::Settings settings;
    settings.system_prompt = "Global prompt.";
    const auto messages = st::build_prompt(card, settings, {});
    CHECK(messages[0].content.find("Character prompt.") != std::string::npos);
    CHECK(messages[0].content.find("Global prompt.") == std::string::npos);
}

TEST(prompt_skips_empty_optional_sections) {
    st::CharacterCard card;
    card.name = "Seraphina";
    card.description = "Only description.";
    st::Settings settings;
    settings.system_prompt = "Global prompt.";
    const auto messages = st::build_prompt(card, settings, {});
    CHECK_EQ(messages.size(), std::size_t(1));
    CHECK(messages[0].content.find("personality") == std::string::npos);
    CHECK(messages[0].content.find("Scenario:") == std::string::npos);
    CHECK(messages[0].content.find("Example dialogues:") == std::string::npos);
}

TEST(prompt_maps_history_roles_and_skips_empty) {
    st::CharacterCard card;
    card.name = "Seraphina";
    st::Settings settings;
    settings.user_name = "Alice";

    st::ChatMessage user;
    user.name = "Alice";
    user.is_user = true;
    user.mes = "Hi {{char}}.";
    st::ChatMessage empty;
    empty.name = "Seraphina";
    empty.mes = "";
    st::ChatMessage bot;
    bot.name = "Seraphina";
    bot.mes = "Hello {{user}}.";
    st::ChatMessage system;
    system.name = "System";
    system.is_system = true;
    system.mes = "Narrate.";

    const auto messages = st::build_prompt(card, settings, {user, empty, bot, system});
    CHECK_EQ(messages.size(), std::size_t(4));
    CHECK_EQ(messages[1].role, "user");
    CHECK_EQ(messages[1].content, "Hi Seraphina.");
    CHECK_EQ(messages[2].role, "assistant");
    CHECK_EQ(messages[2].content, "Hello Alice.");
    CHECK_EQ(messages[3].role, "system");
    CHECK_EQ(messages[3].content, "Narrate.");
}

TEST(prompt_appends_post_history_instructions_last) {
    st::CharacterCard card;
    card.name = "Seraphina";
    card.post_history_instructions = "Be brief, {{user}}.";
    st::Settings settings;
    settings.user_name = "Alice";
    st::ChatMessage user;
    user.name = "Alice";
    user.is_user = true;
    user.mes = "Hi";

    const auto messages = st::build_prompt(card, settings, {user});
    CHECK_EQ(messages.back().role, "system");
    CHECK_EQ(messages.back().content, "Be brief, Alice.");
}

static std::vector<st::ChatMessage> five_message_history() {
    std::vector<st::ChatMessage> history;
    for (int i = 0; i < 5; ++i) {
        st::ChatMessage message;
        message.name = "Alice";
        message.is_user = (i % 2 == 0);
        message.mes = "msg" + std::to_string(i);
        history.push_back(message);
    }
    return history;
}

TEST(prompt_inserts_authors_note_at_depth) {
    st::CharacterCard card;
    card.name = "Seraphina";
    st::Settings settings;
    settings.user_name = "Alice";
    settings.authors_note_enabled = true;
    settings.authors_note = "note";
    settings.authors_note_depth = 2;

    const auto parts =
        st::build_prompt_parts(card, settings, five_message_history());
    CHECK_EQ(parts.history.size(), std::size_t(6));
    CHECK_EQ(parts.history[3].role, "system");
    CHECK_EQ(parts.history[3].content, "note");
    CHECK_EQ(parts.history[4].content, "msg3");
    CHECK_EQ(parts.history[5].content, "msg4");

    // Macro expansion happens exactly like other prompt text.
    settings.authors_note = "For {{char}} and {{user}}";
    const auto expanded =
        st::build_prompt_parts(card, settings, five_message_history());
    CHECK_EQ(expanded.history[3].content, "For Seraphina and Alice");

    // Depth exceeding history length: every history message follows the note.
    settings.authors_note = "note";
    settings.authors_note_depth = 100;
    const auto capped =
        st::build_prompt_parts(card, settings, five_message_history());
    CHECK_EQ(capped.history.size(), std::size_t(6));
    CHECK_EQ(capped.history[0].role, "system");
    CHECK_EQ(capped.history[0].content, "note");
    for (std::size_t i = 1; i < capped.history.size(); ++i) {
        CHECK(capped.history[i].content != "note");
    }
}

TEST(prompt_authors_note_depth_zero_appends_last) {
    st::CharacterCard card;
    card.name = "Seraphina";
    card.post_history_instructions = "Stay in character.";
    st::Settings settings;
    settings.user_name = "Alice";
    settings.authors_note_enabled = true;
    settings.authors_note = "note";
    settings.authors_note_depth = 0;

    const auto parts =
        st::build_prompt_parts(card, settings, five_message_history());
    CHECK_EQ(parts.history.size(), std::size_t(6));
    CHECK_EQ(parts.history.back().role, "system");
    CHECK_EQ(parts.history.back().content, "note");

    // Note lives in history, before post_history_instructions in trailing_system.
    CHECK_EQ(parts.trailing_system.size(), std::size_t(1));
    CHECK_EQ(parts.trailing_system[0].content, "Stay in character.");
    const auto messages =
        st::build_prompt(card, settings, five_message_history());
    // leading system + 6 history (note last) + trailing post_history_instructions
    CHECK_EQ(messages.size(), std::size_t(8));
    CHECK_EQ(messages[6].content, "note");
    CHECK_EQ(messages[7].content, "Stay in character.");
}

TEST(prompt_authors_note_disabled_or_empty_is_absent) {
    st::CharacterCard card;
    card.name = "Seraphina";
    st::Settings settings;
    settings.user_name = "Alice";

    // Disabled (default) even with note text present.
    settings.authors_note_enabled = false;
    settings.authors_note = "note";
    settings.authors_note_depth = 2;
    auto parts = st::build_prompt_parts(card, settings, five_message_history());
    CHECK_EQ(parts.history.size(), std::size_t(5));
    for (const auto& message : parts.history) {
        CHECK(message.content != "note");
    }

    // Enabled but empty note (and whitespace/expansion-empty note).
    settings.authors_note_enabled = true;
    settings.authors_note = "";
    parts = st::build_prompt_parts(card, settings, five_message_history());
    CHECK_EQ(parts.history.size(), std::size_t(5));
    for (const auto& message : parts.history) {
        CHECK(message.content != "note");
    }
}
