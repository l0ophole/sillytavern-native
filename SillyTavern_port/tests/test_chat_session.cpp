#include "test_util.h"
#include "temp_dir.h"

#include "core/chat_session.h"
#include "core/chat_store.h"
#include "core/context_budget.h"
#include "core/prompt.h"

#include <cstddef>
#include <string>
#include <vector>

namespace {

st::CharacterCard make_card() {
    st::CharacterCard card;
    card.name = "Seraphina";
    card.system_prompt = "Stay as {{char}}.";
    card.description = "A dragon who guards a cave.";
    card.post_history_instructions = "Keep replies short.";
    return card;
}

}  // namespace

TEST(prompt_parts_splits_system_and_history) {
    const st::CharacterCard card = make_card();
    st::Settings settings;
    settings.user_name = "Traveler";

    const std::vector<st::ChatMessage> history{
        st::ChatMessage{"Traveler", true, false, "", "Hello {{char}}.", st::Json::object()},
        st::ChatMessage{"Seraphina", false, false, "", "Greetings, {{user}}.", st::Json::object()},
    };

    const st::PromptParts parts = st::build_prompt_parts(card, settings, history);
    CHECK_EQ(parts.leading_system.size(), std::size_t(1));
    CHECK_EQ(parts.leading_system[0].role, "system");
    CHECK(parts.leading_system[0].content.find("Stay as Seraphina.") != std::string::npos);
    CHECK(parts.leading_system[0].content.find("A dragon who guards a cave.") != std::string::npos);

    CHECK_EQ(parts.history.size(), std::size_t(2));
    CHECK_EQ(parts.history[0].role, "user");
    CHECK_EQ(parts.history[0].content, "Hello Seraphina.");
    CHECK_EQ(parts.history[1].role, "assistant");
    CHECK_EQ(parts.history[1].content, "Greetings, Traveler.");

    CHECK_EQ(parts.trailing_system.size(), std::size_t(1));
    CHECK_EQ(parts.trailing_system[0].role, "system");
    CHECK_EQ(parts.trailing_system[0].content, "Keep replies short.");

    const std::vector<st::PromptMessage> combined = st::build_prompt(card, settings, history);
    CHECK_EQ(combined.size(), std::size_t(4));
    CHECK_EQ(combined.front().content, parts.leading_system.front().content);
    CHECK_EQ(combined.back().content, parts.trailing_system.front().content);
}

TEST(session_prompt_respects_budget_and_keeps_post_history) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::CharacterCard card;
    card.name = "Bot";
    card.system_prompt = "System rules.";
    card.post_history_instructions = "Stay in character.";

    st::Settings settings;
    settings.context_size = 80;
    settings.max_tokens = 20;

    st::Chat chat;
    chat.metadata.user_name = "User";
    chat.metadata.character_name = "Bot";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";
    for (int index = 0; index < 10; ++index) {
        st::ChatMessage message;
        message.name = "User";
        message.is_user = true;
        message.send_date = "2026-11-01T00:00:01Z";
        message.mes = std::to_string(index) + std::string(39, 'a');
        chat.messages.push_back(message);
    }

    st::ChatSession session(card, settings, store, "Bot", "chat.jsonl", chat);
    const std::vector<st::PromptMessage> prompt = session.prompt_messages();

    CHECK(session.trimmed());
    CHECK(session.estimated_tokens() <= settings.context_size - settings.max_tokens);
    CHECK_EQ(prompt.front().role, "system");
    CHECK(prompt.front().content.find("System rules.") != std::string::npos);
    CHECK_EQ(prompt.back().role, "system");
    CHECK_EQ(prompt.back().content, "Stay in character.");

    const std::size_t history_count = prompt.size() - 2;
    CHECK(history_count > 0);
    CHECK(history_count < chat.messages.size());
    for (std::size_t index = 0; index < history_count; ++index) {
        CHECK_EQ(prompt[index + 1].content,
                 chat.messages[chat.messages.size() - history_count + index].mes);
    }
}

TEST(session_request_uses_settings_and_sampling) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    settings.api_base_url = "http://example.test/v1";
    settings.api_key = "secret-key";
    settings.model = "test-model";
    settings.temperature = 0.7;
    settings.top_p = 0.8;
    settings.max_tokens = 64;
    settings.stream = false;
    settings.request_timeout_seconds = 42;

    st::Chat chat = store.create("Seraphina", "Traveler", "Hello.");
    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);

    const st::OpenAiRequest request = session.request();
    CHECK_EQ(request.base_url, settings.api_base_url);
    CHECK_EQ(request.api_key, settings.api_key);
    CHECK_EQ(request.model, settings.model);
    CHECK_EQ(request.sampling.temperature, settings.temperature);
    CHECK_EQ(request.sampling.top_p, settings.top_p);
    CHECK_EQ(request.sampling.max_tokens, settings.max_tokens);
    CHECK_EQ(request.sampling.stream, settings.stream);
    CHECK_EQ(request.timeout_seconds, settings.request_timeout_seconds);
    CHECK_EQ(request.messages.size(), session.prompt_messages().size());
    CHECK_EQ(request.messages.front().role, "system");
}

TEST(session_append_and_save_round_trip) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    settings.user_name = "Traveler";

    st::Chat chat = store.create("Seraphina", "Traveler", "Hello there.");
    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);

    session.append_user("Hi, Seraphina.");
    CHECK_EQ(session.chat().messages.size(), std::size_t(2));
    const st::ChatMessage& user = session.chat().messages.back();
    CHECK(user.is_user);
    CHECK(!user.is_system);
    CHECK_EQ(user.name, "Traveler");
    CHECK_EQ(user.mes, "Hi, Seraphina.");
    CHECK_EQ(user.send_date.size(), std::size_t(20));
    CHECK_EQ(user.send_date.back(), 'Z');

    session.append_assistant("Welcome, traveler.");
    CHECK_EQ(session.chat().messages.size(), std::size_t(3));
    const st::ChatMessage& assistant = session.chat().messages.back();
    CHECK(!assistant.is_user);
    CHECK(!assistant.is_system);
    CHECK_EQ(assistant.name, "Seraphina");
    CHECK_EQ(assistant.mes, "Welcome, traveler.");
    CHECK_EQ(assistant.send_date.size(), std::size_t(20));
    CHECK_EQ(assistant.send_date.back(), 'Z');

    const std::vector<st::PromptMessage> prompt = session.prompt_messages();
    CHECK(!prompt.empty());
    CHECK_EQ(prompt.front().role, "system");
    CHECK_EQ(prompt.back().role, "system");
    CHECK_EQ(prompt.back().content, "Keep replies short.");
    CHECK_EQ(prompt[prompt.size() - 2].role, "assistant");
    CHECK_EQ(prompt[prompt.size() - 2].content, "Welcome, traveler.");

    session.save();
    const st::Chat loaded = store.load("Seraphina", "chat.jsonl");
    CHECK_EQ(loaded.messages.size(), std::size_t(3));
    CHECK_EQ(loaded.messages[0].mes, "Hello there.");
    CHECK_EQ(loaded.messages[1].name, "Traveler");
    CHECK(loaded.messages[1].is_user);
    CHECK_EQ(loaded.messages[1].mes, "Hi, Seraphina.");
    CHECK_EQ(loaded.messages[2].name, "Seraphina");
    CHECK(!loaded.messages[2].is_user);
    CHECK_EQ(loaded.messages[2].mes, "Welcome, traveler.");
}
