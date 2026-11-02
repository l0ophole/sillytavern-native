#include "test_util.h"
#include "temp_dir.h"

#include "core/atomic_file.h"
#include "core/chat_store.h"

#include <filesystem>
#include <string>

TEST(chat_create_includes_greeting) {
    TempDir temp;
    st::ChatStore store(temp.path());
    const auto chat = store.create("Seraphina", "User", "Hello there.");
    CHECK_EQ(chat.metadata.character_name, "Seraphina");
    CHECK_EQ(chat.metadata.user_name, "User");
    CHECK_EQ(chat.messages.size(), std::size_t(1));
    CHECK_EQ(chat.messages[0].name, "Seraphina");
    CHECK(!chat.messages[0].is_user);
    CHECK_EQ(chat.messages[0].mes, "Hello there.");
    CHECK(!chat.metadata.create_date.empty());
    CHECK_EQ(chat.metadata.create_date.back(), 'Z');
    CHECK(!chat.messages[0].send_date.empty());
    CHECK_EQ(chat.messages[0].send_date.back(), 'Z');
}

TEST(chat_create_without_greeting_has_no_messages) {
    TempDir temp;
    st::ChatStore store(temp.path());
    const auto chat = store.create("Seraphina", "User", "");
    CHECK(chat.messages.empty());
}

TEST(chat_roundtrip_preserves_order_and_unknown_fields) {
    TempDir temp;
    st::ChatStore store(temp.path());
    st::Chat chat;
    chat.metadata.user_name = "User";
    chat.metadata.character_name = "Seraphina";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";
    chat.metadata.chat_metadata["scenario"] = "cave";
    chat.metadata.extra["future_meta"] = true;

    st::ChatMessage user;
    user.name = "User";
    user.is_user = true;
    user.send_date = "2026-11-01T00:00:01Z";
    user.mes = "Hi";
    user.extra["future_msg"] = 42;

    st::ChatMessage bot;
    bot.name = "Seraphina";
    bot.is_user = false;
    bot.send_date = "2026-11-01T00:00:02Z";
    bot.mes = "Hello";

    chat.messages = {user, bot};

    store.save("Seraphina", "chat.jsonl", chat);
    const auto loaded = store.load("Seraphina", "chat.jsonl");
    CHECK_EQ(loaded.metadata.user_name, "User");
    CHECK_EQ(loaded.metadata.character_name, "Seraphina");
    CHECK_EQ(loaded.metadata.create_date, "2026-11-01T00:00:00Z");
    CHECK_EQ(loaded.metadata.chat_metadata["scenario"].get<std::string>(), "cave");
    CHECK_EQ(loaded.metadata.extra["future_meta"].get<bool>(), true);
    CHECK_EQ(loaded.messages.size(), std::size_t(2));
    CHECK(loaded.messages[0].is_user);
    CHECK_EQ(loaded.messages[0].name, "User");
    CHECK_EQ(loaded.messages[0].mes, "Hi");
    CHECK_EQ(loaded.messages[0].send_date, "2026-11-01T00:00:01Z");
    CHECK_EQ(loaded.messages[0].extra["future_msg"].get<int>(), 42);
    CHECK_EQ(loaded.messages[1].name, "Seraphina");
    CHECK(!loaded.messages[1].is_user);
    CHECK_EQ(loaded.messages[1].mes, "Hello");
}

TEST(chat_roundtrip_preserves_system_role) {
    TempDir temp;
    st::ChatStore store(temp.path());
    st::Chat chat = store.create("Seraphina", "User", "Hi");
    st::ChatMessage system;
    system.name = "System";
    system.is_system = true;
    system.mes = "Narrate only.";
    chat.messages.push_back(system);
    store.save("Seraphina", "system.jsonl", chat);

    const auto loaded = store.load("Seraphina", "system.jsonl");
    CHECK_EQ(loaded.messages.size(), std::size_t(2));
    CHECK(loaded.messages[1].is_system);
    CHECK(!loaded.messages[1].is_user);
    CHECK_EQ(loaded.messages[1].mes, "Narrate only.");
}

TEST(chat_create_uses_display_name) {
    TempDir temp;
    st::ChatStore store(temp.path());
    const auto chat = store.create("id-1", "User", "Hello", "Seraphina");
    CHECK_EQ(chat.metadata.character_name, "Seraphina");
    CHECK_EQ(chat.messages.size(), std::size_t(1));
    CHECK_EQ(chat.messages[0].name, "Seraphina");
}

TEST(chat_load_rejects_oversized_file) {
    TempDir temp;
    st::ChatStore store(temp.path());
    const auto path = store.chat_dir("Seraphina") / "big.jsonl";
    st::write_file_atomic(path, "");
    std::filesystem::resize_file(path, 64u * 1024u * 1024u + 1u);
    bool threw = false;
    try {
        (void)store.load("Seraphina", "big.jsonl");
    } catch (const std::runtime_error& error) {
        threw = true;
        const std::string message = error.what();
        CHECK(message.find("too large") != std::string::npos);
    }
    CHECK(threw);
}

TEST(chat_list_rename_remove) {
    TempDir temp;
    st::ChatStore store(temp.path());
    const auto chat = store.create("Seraphina", "User", "Hi");
    store.save("Seraphina", "a.jsonl", chat);
    store.save("Seraphina", "b.jsonl", chat);
    CHECK_EQ(store.list("Seraphina").size(), std::size_t(2));

    store.rename("Seraphina", "a.jsonl", "c.jsonl");
    const auto names = store.list("Seraphina");
    CHECK_EQ(names.size(), std::size_t(2));
    CHECK_EQ(names[0], "b.jsonl");
    CHECK_EQ(names[1], "c.jsonl");

    store.remove("Seraphina", "b.jsonl");
    const auto remaining = store.list("Seraphina");
    CHECK_EQ(remaining.size(), std::size_t(1));
    CHECK_EQ(remaining[0], "c.jsonl");
}

TEST(chat_malformed_line_reports_location) {
    TempDir temp;
    st::ChatStore store(temp.path());
    const auto dir = store.chat_dir("Seraphina");
    st::write_file_atomic(dir / "bad.jsonl",
                          R"({"user_name":"User","character_name":"Seraphina","create_date":"x"})" "\n"
                          "{not json}\n");
    bool threw = false;
    try {
        store.load("Seraphina", "bad.jsonl");
    } catch (const std::runtime_error& error) {
        threw = true;
        const std::string message = error.what();
        CHECK(message.find("bad.jsonl") != std::string::npos);
        CHECK(message.find("2") != std::string::npos);
    }
    CHECK(threw);
}

TEST(chat_rejects_path_escape) {
    TempDir temp;
    st::ChatStore store(temp.path());
    bool character_threw = false;
    try {
        store.load("../../etc", "passwd.jsonl");
    } catch (const std::runtime_error&) {
        character_threw = true;
    }
    CHECK(character_threw);

    bool filename_threw = false;
    try {
        store.load("Seraphina", "../escape.jsonl");
    } catch (const std::runtime_error&) {
        filename_threw = true;
    }
    CHECK(filename_threw);

    const auto dir = store.chat_dir("../../etc");
    CHECK_EQ(dir.parent_path(), temp.path());
}

TEST(chat_list_ignores_non_jsonl) {
    TempDir temp;
    st::ChatStore store(temp.path());
    const auto dir = store.chat_dir("Seraphina");
    st::write_file_atomic(dir / "notes.txt", "not a chat");
    st::write_file_atomic(dir / "chat.jsonl", "{}");
    const auto names = store.list("Seraphina");
    CHECK_EQ(names.size(), std::size_t(1));
    CHECK_EQ(names[0], "chat.jsonl");
}

TEST(chat_rename_refuses_to_overwrite) {
    TempDir temp;
    st::ChatStore store(temp.path());
    const auto chat = store.create("Seraphina", "User", "Hi");
    store.save("Seraphina", "a.jsonl", chat);
    store.save("Seraphina", "b.jsonl", chat);
    bool threw = false;
    try {
        store.rename("Seraphina", "a.jsonl", "b.jsonl");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK_EQ(store.list("Seraphina").size(), std::size_t(2));
}
