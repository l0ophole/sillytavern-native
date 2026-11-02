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
