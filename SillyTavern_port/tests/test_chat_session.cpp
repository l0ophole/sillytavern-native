#include "test_util.h"
#include "temp_dir.h"

#include "core/chat_session.h"
#include "core/chat_store.h"
#include "core/context_budget.h"
#include "core/prompt.h"

#include <cstddef>
#include <filesystem>
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

TEST(session_edit_preserves_extra_and_timestamp) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat;
    chat.metadata.user_name = "Traveler";
    chat.metadata.character_name = "Seraphina";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";

    st::ChatMessage message;
    message.name = "Traveler";
    message.is_user = true;
    message.send_date = "2026-11-01T00:00:01Z";
    message.mes = "before";
    message.extra["future_field"] = 7;
    chat.messages.push_back(message);

    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    CHECK(session.edit_message(0, "after"));
    const st::ChatMessage& edited = session.chat().messages[0];
    CHECK_EQ(edited.mes, "after");
    CHECK_EQ(edited.send_date, "2026-11-01T00:00:01Z");
    CHECK(edited.is_user);
    CHECK(!edited.is_system);
    CHECK_EQ(edited.name, "Traveler");
    CHECK_EQ(edited.extra["future_field"].get<int>(), 7);

    CHECK(!session.edit_message(5, "nope"));
    CHECK_EQ(session.chat().messages.size(), std::size_t(1));
    CHECK_EQ(session.chat().messages[0].mes, "after");
}

TEST(session_delete_and_regenerate_boundaries) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat;
    chat.metadata.user_name = "Traveler";
    chat.metadata.character_name = "Seraphina";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";

    for (int index = 0; index < 4; ++index) {
        st::ChatMessage message;
        message.name = (index % 2 == 0) ? "Traveler" : "Seraphina";
        message.is_user = (index % 2 == 0);
        message.send_date = "2026-11-01T00:00:01Z";
        message.mes = "message " + std::to_string(index);
        chat.messages.push_back(message);
    }

    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    CHECK(session.delete_message(1));
    CHECK_EQ(session.chat().messages.size(), std::size_t(3));
    CHECK_EQ(session.chat().messages[0].mes, "message 0");
    CHECK_EQ(session.chat().messages[1].mes, "message 2");
    CHECK_EQ(session.chat().messages[2].mes, "message 3");

    CHECK(!session.delete_message(99));
    CHECK_EQ(session.chat().messages.size(), std::size_t(3));

    CHECK(session.regenerate());
    CHECK_EQ(session.chat().messages.size(), std::size_t(2));
    CHECK_EQ(session.chat().messages.back().mes, "message 2");
    CHECK(!session.regenerate());
    CHECK_EQ(session.chat().messages.size(), std::size_t(2));

    session.save();
    const st::Chat loaded = store.load("Seraphina", "chat.jsonl");
    CHECK_EQ(loaded.messages.size(), std::size_t(2));
    CHECK_EQ(loaded.messages[0].mes, "message 0");
    CHECK_EQ(loaded.messages[1].mes, "message 2");

    st::Chat empty;
    st::ChatSession empty_session(make_card(), settings, store, "Seraphina", "empty.jsonl", empty);
    CHECK(!empty_session.regenerate());
    CHECK(!empty_session.delete_message(0));
}

TEST(session_snapshot_restores_after_failed_action) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat = store.create("Seraphina", "Traveler", "Hello.");
    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);

    session.append_user("Keep me");
    const st::Chat before = session.snapshot();
    CHECK_EQ(session.chat().messages.size(), std::size_t(2));

    session.append_assistant("Temporary reply");
    CHECK(session.edit_message(0, "Changed greeting"));
    CHECK(session.delete_message(2));
    CHECK(session.delete_message(1));
    CHECK_EQ(session.chat().messages.size(), std::size_t(1));

    session.restore(before);
    CHECK_EQ(session.chat().messages.size(), std::size_t(2));
    CHECK_EQ(session.chat().messages[0].mes, "Hello.");
    CHECK_EQ(session.chat().messages[1].mes, "Keep me");
}

TEST(session_action_guards) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat;
    chat.metadata.user_name = "Traveler";
    chat.metadata.character_name = "Seraphina";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";

    st::ChatMessage greeting;
    greeting.name = "Seraphina";
    greeting.mes = "Hello";
    st::ChatMessage user;
    user.name = "Traveler";
    user.is_user = true;
    user.mes = "Hi";
    st::ChatMessage assistant;
    assistant.name = "Seraphina";
    assistant.mes = "Welcome";
    chat.messages = {greeting, user, assistant};

    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    CHECK(session.can_regenerate_at(2));
    CHECK(session.can_continue_at(2));
    CHECK(!session.can_regenerate_at(1));
    CHECK(!session.can_continue_at(1));
    CHECK(!session.can_regenerate_at(0));
    CHECK(!session.can_continue_at(0));
    CHECK(!session.can_regenerate_at(99));
    CHECK(!session.can_continue_at(99));

    session.set_filename("renamed.jsonl");
    session.save();
    CHECK(std::filesystem::exists(store.chat_dir("Seraphina") / "renamed.jsonl"));
}

TEST(session_continuation_adds_separator) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat;
    chat.metadata.user_name = "Traveler";
    chat.metadata.character_name = "Seraphina";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";

    st::ChatMessage assistant;
    assistant.name = "Seraphina";
    assistant.mes = "Hello";
    chat.messages = {assistant};

    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    session.append_to_last_assistant("world");
    CHECK_EQ(session.chat().messages.back().mes, "Hello world");

    session.append_to_last_assistant(" again");
    CHECK_EQ(session.chat().messages.back().mes, "Hello world again");

    session.append_to_last_assistant("!");
    CHECK_EQ(session.chat().messages.back().mes, "Hello world again!");
}

TEST(session_recover_draft_appends_partial) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat = store.create("Seraphina", "Traveler", "Hello.");
    st::ChatMessage user;
    user.name = "Traveler";
    user.is_user = true;
    user.send_date = "2026-11-01T00:00:01Z";
    user.mes = "Hi";
    chat.messages.push_back(user);

    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    const st::ChatDraft draft{"partial reply", false, "2026-11-01T00:00:02Z", 2};
    CHECK(session.recover_draft(draft));
    CHECK_EQ(session.chat().messages.size(), std::size_t(3));
    const st::ChatMessage& last = session.chat().messages.back();
    CHECK_EQ(last.name, "Seraphina");
    CHECK(!last.is_user);
    CHECK(!last.is_system);
    CHECK_EQ(last.send_date, "2026-11-01T00:00:02Z");
    CHECK_EQ(last.mes, "partial reply");
}

TEST(session_recover_draft_continues_without_separator_duplication) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat;
    chat.metadata.user_name = "Traveler";
    chat.metadata.character_name = "Seraphina";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";
    st::ChatMessage assistant;
    assistant.name = "Seraphina";
    assistant.mes = "Hello";
    chat.messages = {assistant};

    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    CHECK(session.recover_draft(st::ChatDraft{"world", true, "", 1}));
    CHECK_EQ(session.chat().messages.size(), std::size_t(1));
    CHECK_EQ(session.chat().messages.back().mes, "Hello world");

    st::Chat spaced = chat;
    spaced.messages[0].mes = "Hello ";
    st::ChatSession spaced_session(
        make_card(), settings, store, "Seraphina", "spaced.jsonl", spaced);
    CHECK(spaced_session.recover_draft(st::ChatDraft{"world", true, "", 1}));
    CHECK_EQ(spaced_session.chat().messages.back().mes, "Hello world");
}

TEST(session_recover_draft_rejects_stale_base) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat;
    chat.metadata.user_name = "Traveler";
    chat.metadata.character_name = "Seraphina";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";
    st::ChatMessage greeting;
    greeting.name = "Seraphina";
    greeting.mes = "Hello";
    st::ChatMessage user;
    user.name = "Traveler";
    user.is_user = true;
    user.mes = "Hi";
    chat.messages = {greeting, user};

    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    CHECK(!session.recover_draft(st::ChatDraft{"stale reply", false, "", 1}));
    CHECK_EQ(session.chat().messages.size(), std::size_t(2));
    CHECK_EQ(session.chat().messages.back().mes, "Hi");
    CHECK(session.chat().messages.back().is_user);
}

TEST(chat_session_replaces_last_message_on_regenerate_recovery) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat;
    chat.metadata.user_name = "Traveler";
    chat.metadata.character_name = "Seraphina";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";
    st::ChatMessage user;
    user.name = "Traveler";
    user.is_user = true;
    user.send_date = "2026-11-01T00:00:01Z";
    user.mes = "Hi";
    st::ChatMessage assistant;
    assistant.name = "Seraphina";
    assistant.is_user = false;
    assistant.is_system = false;
    assistant.send_date = "2026-11-01T00:00:02Z";
    assistant.mes = "old";
    chat.messages = {user, assistant};

    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    st::ChatDraft draft;
    draft.text = "new";
    draft.send_date = "2026-11-01T00:00:03Z";
    draft.base_messages = 1;
    draft.replace_last = true;
    CHECK(session.recover_draft(draft));
    CHECK_EQ(session.chat().messages.size(), 2u);
    CHECK_EQ(session.chat().messages.back().mes, "new");
    CHECK_EQ(session.chat().messages.back().send_date, "2026-11-01T00:00:03Z");
    CHECK_EQ(session.chat().messages.front().mes, "Hi");
    CHECK_EQ(session.swipe_count(1), 2u);
    CHECK_EQ(session.swipe_index(1), 1u);
    CHECK(session.select_swipe(1, 0));
    CHECK_EQ(session.chat().messages.back().mes, "old");
}

TEST(chat_session_rejects_regenerate_draft_when_base_is_stale) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat;
    chat.metadata.user_name = "Traveler";
    chat.metadata.character_name = "Seraphina";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";
    st::ChatMessage user;
    user.name = "Traveler";
    user.is_user = true;
    user.send_date = "2026-11-01T00:00:01Z";
    user.mes = "Hi";
    st::ChatMessage assistant_old;
    assistant_old.name = "Seraphina";
    assistant_old.send_date = "2026-11-01T00:00:02Z";
    assistant_old.mes = "old";
    st::ChatMessage assistant_second;
    assistant_second.name = "Seraphina";
    assistant_second.send_date = "2026-11-01T00:00:03Z";
    assistant_second.mes = "second";
    chat.messages = {user, assistant_old, assistant_second};

    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    st::ChatDraft draft;
    draft.text = "new";
    draft.send_date = "2026-11-01T00:00:04Z";
    draft.base_messages = 1;
    draft.replace_last = true;
    CHECK(!session.recover_draft(draft));
    CHECK_EQ(session.chat().messages.size(), 3u);
    CHECK_EQ(session.chat().messages.back().mes, "second");
    CHECK_EQ(session.chat().messages.back().send_date, "2026-11-01T00:00:03Z");
    CHECK_EQ(session.chat().messages[1].mes, "old");

    // Also rejects when the last message is not an assistant.
    st::Chat user_tail;
    user_tail.metadata.user_name = "Traveler";
    user_tail.metadata.character_name = "Seraphina";
    user_tail.metadata.create_date = "2026-11-01T00:00:00Z";
    user_tail.messages = {user, assistant_old, user};
    st::ChatSession tail_session(make_card(), settings, store, "Seraphina", "tail.jsonl", user_tail);
    st::ChatDraft tail_draft;
    tail_draft.text = "new";
    tail_draft.base_messages = 2;
    tail_draft.replace_last = true;
    CHECK(!tail_session.recover_draft(tail_draft));
    CHECK_EQ(tail_session.chat().messages.size(), 3u);
    CHECK_EQ(tail_session.chat().messages.back().mes, "Hi");

    // Also rejects an empty draft.
    st::Chat two;
    two.metadata.user_name = "Traveler";
    two.metadata.character_name = "Seraphina";
    two.metadata.create_date = "2026-11-01T00:00:00Z";
    two.messages = {user, assistant_old};
    st::ChatSession empty_session(make_card(), settings, store, "Seraphina", "empty.jsonl", two);
    st::ChatDraft empty_draft;
    empty_draft.text = "";
    empty_draft.base_messages = 1;
    empty_draft.replace_last = true;
    CHECK(!empty_session.recover_draft(empty_draft));
    CHECK_EQ(empty_session.chat().messages.size(), 2u);
    CHECK_EQ(empty_session.chat().messages.back().mes, "old");
}

TEST(session_continue_requires_assistant_tail) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat;
    chat.metadata.user_name = "Traveler";
    chat.metadata.character_name = "Seraphina";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";

    st::ChatMessage greeting;
    greeting.name = "Seraphina";
    greeting.mes = "Hello";
    st::ChatMessage user;
    user.name = "Traveler";
    user.is_user = true;
    user.mes = "Hi";
    st::ChatMessage assistant;
    assistant.name = "Seraphina";
    assistant.mes = "Welcome";
    chat.messages = {greeting, user, assistant};

    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    CHECK(session.can_continue());
    CHECK_EQ(session.continue_prefix(), "Welcome");
    session.append_to_last_assistant(" friend");
    CHECK_EQ(session.chat().messages.back().mes, "Welcome friend");
    CHECK_EQ(session.chat().messages.size(), std::size_t(3));

    st::Chat user_tail;
    user_tail.metadata.user_name = "Traveler";
    user_tail.metadata.character_name = "Seraphina";
    user_tail.metadata.create_date = "2026-11-01T00:00:00Z";
    user_tail.messages = {user};
    st::ChatSession user_session(make_card(), settings, store, "Seraphina", "user.jsonl", user_tail);
    CHECK(!user_session.can_continue());
    CHECK(user_session.continue_prefix().empty());
    user_session.append_to_last_assistant("ignored");
    CHECK_EQ(user_session.chat().messages.back().mes, "Hi");

    st::Chat trailing_empty;
    trailing_empty.metadata.user_name = "Traveler";
    trailing_empty.metadata.character_name = "Seraphina";
    trailing_empty.metadata.create_date = "2026-11-01T00:00:00Z";
    st::ChatMessage empty_user;
    empty_user.name = "Traveler";
    empty_user.is_user = true;
    empty_user.mes = "";
    trailing_empty.messages = {greeting, empty_user};
    st::ChatSession trailing_session(
        make_card(), settings, store, "Seraphina", "trailing.jsonl", trailing_empty);
    CHECK(trailing_session.can_continue());
    CHECK_EQ(trailing_session.continue_prefix(), "Hello");
}

TEST(chat_session_swipes_default_to_the_message_text) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat;
    chat.metadata.user_name = "Traveler";
    chat.metadata.character_name = "Seraphina";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";
    st::ChatMessage message;
    message.name = "Seraphina";
    message.mes = "hello";
    chat.messages = {message};

    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    CHECK_EQ(session.swipe_count(0), 1u);
    CHECK_EQ(session.swipe_index(0), 0u);
    CHECK_EQ(session.chat().messages[0].mes, "hello");
}

TEST(chat_session_add_and_select_swipe) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat;
    chat.metadata.user_name = "Traveler";
    chat.metadata.character_name = "Seraphina";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";

    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    session.append_assistant("one");
    CHECK(session.add_swipe(0, "two"));
    CHECK_EQ(session.swipe_count(0), 2u);
    CHECK_EQ(session.swipe_index(0), 1u);
    CHECK_EQ(session.chat().messages[0].mes, "two");
    CHECK(session.select_swipe(0, 0));
    CHECK_EQ(session.chat().messages[0].mes, "one");
    CHECK(!session.select_swipe(0, 7));
    CHECK_EQ(session.chat().messages[0].mes, "one");
}

TEST(chat_session_swipes_survive_save_and_load) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat;
    chat.metadata.user_name = "Traveler";
    chat.metadata.character_name = "Seraphina";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";

    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    session.append_assistant("one");
    CHECK(session.add_swipe(0, "two"));
    CHECK(session.select_swipe(0, 0));
    session.save();

    const st::Chat loaded = store.load("Seraphina", "chat.jsonl");
    CHECK_EQ(loaded.messages.size(), 1u);
    CHECK_EQ(loaded.messages[0].extra["swipes"].size(), 2u);
    CHECK_EQ(loaded.messages[0].extra["swipe_id"].get<int>(), 0);
    CHECK_EQ(loaded.messages[0].mes, "one");
}

TEST(chat_session_branch_copies_prefix_only) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat = store.create("Seraphina", "Traveler", "Hello there.");
    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    session.append_user("message 1");
    session.append_assistant("message 2");
    session.append_user("message 3");
    CHECK_EQ(session.chat().messages.size(), 4u);

    const st::Chat branch = session.branch(1);
    CHECK_EQ(branch.messages.size(), 2u);
    CHECK_EQ(branch.messages[1].mes, session.chat().messages[1].mes);
    CHECK_EQ(branch.messages[1].mes, "message 1");
    CHECK_EQ(session.chat().messages.size(), 4u);
    CHECK_EQ(session.chat().messages[3].mes, "message 3");
    CHECK(!branch.metadata.create_date.empty());

    const st::Chat empty = session.branch(99);
    CHECK(empty.messages.empty());
    CHECK_EQ(session.chat().messages.size(), 4u);
}

TEST(chat_session_bookmarks_round_trip) {
    TempDir temp;
    st::ChatStore store(temp.path());

    st::Settings settings;
    st::Chat chat = store.create("Seraphina", "Traveler", "Hello there.");
    st::ChatSession session(make_card(), settings, store, "Seraphina", "chat.jsonl", chat);
    session.append_user("message 1");
    session.append_assistant("message 2");

    CHECK(session.add_bookmark(2, "here"));
    CHECK(!session.add_bookmark(99, "nowhere"));
    const auto bookmarks = session.bookmarks();
    CHECK_EQ(bookmarks.size(), 1u);
    CHECK_EQ(bookmarks[0].first, 2u);
    CHECK_EQ(bookmarks[0].second, "here");

    session.save();
    const st::Chat loaded = store.load("Seraphina", "chat.jsonl");
    st::ChatSession reloaded_session(
        make_card(), settings, store, "Seraphina", "chat.jsonl", loaded);
    const auto loaded_bookmarks = reloaded_session.bookmarks();
    CHECK_EQ(loaded_bookmarks.size(), 1u);
    CHECK_EQ(loaded_bookmarks[0].first, 2u);
    CHECK_EQ(loaded_bookmarks[0].second, "here");

    CHECK(session.remove_bookmark(2));
    CHECK(session.bookmarks().empty());
    CHECK(!session.remove_bookmark(2));
}
