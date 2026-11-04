#include "test_util.h"

#include "core/character_card.h"
#include "core/chat_types.h"
#include "core/persona.h"
#include "core/prompt.h"
#include "core/settings.h"
#include "core/world_info.h"

#include <string>

TEST(persona_from_json_tolerant_and_skips_empty) {
    st::PersonaStore store = st::PersonaStore::from_json(st::parse_json(
        R"({"personas":[{"name":"Mia","description":"Brave.","header":"Hello."},{"name":"","description":"skip"},{"description":"no name"}],"active":"Mia"})",
        "personas"));
    CHECK_EQ(store.personas.size(), std::size_t(1));
    CHECK(store.active() != nullptr);
    CHECK_EQ(store.active()->name, "Mia");
    CHECK_EQ(store.active()->description, "Brave.");
    CHECK_EQ(store.active()->header, "Hello.");
    CHECK_EQ(store.active_name, "Mia");

    st::PersonaStore empty = st::PersonaStore::from_json(st::parse_json("{}", "personas"));
    CHECK(empty.personas.empty());
    CHECK(empty.active() == nullptr);
}

TEST(persona_to_json_round_trips) {
    st::PersonaStore store;
    st::Persona persona;
    persona.name = "Mia";
    persona.description = "Brave.";
    store.personas.push_back(persona);
    store.active_name = "Mia";
    const st::Json json = store.to_json();
    CHECK_EQ(json["active"].get<std::string>(), "Mia");
    CHECK_EQ(json["personas"].size(), std::size_t(1));
    CHECK_EQ(json["personas"][0]["name"].get<std::string>(), "Mia");
    CHECK_EQ(json["personas"][0]["description"].get<std::string>(), "Brave.");
    st::PersonaStore restored = st::PersonaStore::from_json(json);
    CHECK(restored.active() != nullptr);
    CHECK_EQ(restored.active()->name, "Mia");
}

TEST(persona_active_expands_user) {
    st::CharacterCard card;
    card.name = "Seraphina";
    card.description = "hi {{user}}";
    st::Settings settings;
    settings.user_name = "Alice";
    st::PersonaStore store;
    st::Persona persona;
    persona.name = "Mia";
    store.personas.push_back(persona);
    store.active_name = "Mia";
    const auto parts = st::build_prompt_parts(card, settings, {}, &store);
    CHECK(!parts.leading_system.empty());
    CHECK(parts.leading_system[0].content.find("hi Mia") != std::string::npos);
}

TEST(prompt_injects_persona_once) {
    st::CharacterCard card;
    card.name = "Seraphina";
    card.description = "A dragon.";
    st::Settings settings;
    settings.user_name = "Alice";
    st::PersonaStore store;
    st::Persona persona;
    persona.name = "Mia";
    persona.description = "A brave explorer.";
    store.personas.push_back(persona);
    store.active_name = "Mia";
    const auto parts = st::build_prompt_parts(card, settings, {}, &store);
    std::size_t counts = 0;
    for (const auto& message : parts.leading_system) {
        if (message.content.find("User persona (Mia):") != std::string::npos) {
            ++counts;
        }
    }
    CHECK_EQ(counts, 1u);
    // Empty description injects nothing.
    store.personas[0].description = "";
    const auto without = st::build_prompt_parts(card, settings, {}, &store);
    for (const auto& message : without.leading_system) {
        CHECK(message.content.find("User persona") == std::string::npos);
    }
}

TEST(persona_empty_leaves_prompt_unchanged) {
    st::CharacterCard card;
    card.name = "Seraphina";
    st::Settings settings;
    settings.user_name = "Alice";
    const auto legacy = st::build_prompt_parts(card, settings, {});
    st::PersonaStore empty_store;
    const auto with_empty = st::build_prompt_parts(card, settings, {}, &empty_store);
    CHECK_EQ(with_empty.leading_system.size(), legacy.leading_system.size());
    for (std::size_t i = 0; i < legacy.leading_system.size(); ++i) {
        CHECK_EQ(with_empty.leading_system[i].content, legacy.leading_system[i].content);
    }
}

TEST(persona_description_after_char_before_world_info) {
    st::CharacterCard card;
    card.name = "Seraphina";
    card.description = "A dragon.";
    st::Settings settings;
    settings.user_name = "Alice";
    settings.world_info_enabled = true;
    st::PersonaStore store;
    st::Persona persona;
    persona.name = "Mia";
    persona.description = "Brave.";
    store.personas.push_back(persona);
    store.active_name = "Mia";
    st::WorldInfoBook book;
    book.entries = {{"dragon", "", "Dragons are rare.", true, 0, 0}};
    st::ChatMessage user;
    user.name = "Alice";
    user.is_user = true;
    user.mes = "I saw a dragon";
    const auto parts = st::build_prompt_parts_with_book(card, settings, {user}, book, &store);
    std::size_t persona_idx = parts.leading_system.size();
    std::size_t char_idx = parts.leading_system.size();
    std::size_t wi_idx = parts.leading_system.size();
    for (std::size_t i = 0; i < parts.leading_system.size(); ++i) {
        if (parts.leading_system[i].content.find("User persona (Mia): Brave.") !=
            std::string::npos) {
            persona_idx = i;
        }
        if (parts.leading_system[i].content.find("A dragon.") != std::string::npos) {
            char_idx = i;
        }
        if (parts.leading_system[i].content.find("World Info:") != std::string::npos) {
            wi_idx = i;
        }
    }
    CHECK(persona_idx < parts.leading_system.size());
    CHECK(char_idx <= persona_idx);
    CHECK(persona_idx < wi_idx);
}
