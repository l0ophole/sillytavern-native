#include "test_util.h"
#include "temp_dir.h"

#include "core/atomic_file.h"
#include "core/backup.h"

#include <filesystem>
#include <string>

namespace {

void write_chat(const std::filesystem::path& root,
                const std::string& character,
                const std::string& filename,
                const std::string& content) {
    st::write_file_atomic(root / character / filename, content);
}

}  // namespace

TEST(backup_create_and_list_round_trip) {
    TempDir temp;
    write_chat(temp.path(), "Seraphina", "chat.jsonl", "{\"hello\":\"one\"}\n");
    write_chat(temp.path(), "Bob", "chat.jsonl", "{\"hello\":\"two\"}\n");

    st::BackupManager manager(temp.path());
    const std::string id = manager.create("manual");
    CHECK(!id.empty());
    CHECK(id.find("manual") != std::string::npos);
    CHECK(std::filesystem::is_directory(temp.path() / ".backups" / id));
    CHECK(std::filesystem::is_regular_file(temp.path() / ".backups" / id / "manifest.json"));
    CHECK(std::filesystem::is_regular_file(
        temp.path() / ".backups" / id / "Seraphina" / "chat.jsonl"));

    const auto entries = manager.list();
    CHECK_EQ(entries.size(), 1u);
    CHECK_EQ(entries[0].id, id);
    CHECK_EQ(entries[0].chats, 2u);
    CHECK(entries[0].bytes > 0u);
    CHECK(!entries[0].created.empty());
    CHECK_EQ(entries[0].path, temp.path() / ".backups" / id);
}

TEST(backup_restore_recovers_deleted_chat) {
    TempDir temp;
    const std::string original = "{\"hello\":\"original\"}\n";
    write_chat(temp.path(), "Seraphina", "chat.jsonl", original);
    write_chat(temp.path(), "Bob", "chat.jsonl", "{\"hello\":\"two\"}\n");

    st::BackupManager manager(temp.path());
    const std::string id = manager.create("manual");

    std::filesystem::remove(temp.path() / "Seraphina" / "chat.jsonl");
    write_chat(temp.path(), "Bob", "extra.jsonl", "{\"hello\":\"extra\"}\n");

    manager.restore(id);

    CHECK_EQ(st::read_file(temp.path() / "Seraphina" / "chat.jsonl"), original);
    CHECK(std::filesystem::is_regular_file(temp.path() / "Bob" / "extra.jsonl"));
}

TEST(backup_restore_failure_leaves_existing_files_untouched) {
    TempDir temp;
    write_chat(temp.path(), "Alice", "a.jsonl", "{\"hello\":\"alice-original\"}\n");
    write_chat(temp.path(), "Zed", "z.jsonl", "{\"hello\":\"zed-original\"}\n");

    st::BackupManager manager(temp.path());
    const std::string id = manager.create("manual");

    const std::string alice_modified = "{\"hello\":\"alice-modified\"}\n";
    const std::string zed_current = "{\"hello\":\"zed-current\"}\n";
    write_chat(temp.path(), "Alice", "a.jsonl", alice_modified);
    write_chat(temp.path(), "Zed", "z.jsonl", zed_current);

    const auto backup_z = temp.path() / ".backups" / id / "Zed" / "z.jsonl";
    std::filesystem::resize_file(backup_z, 64u * 1024u * 1024u + 1u);

    bool threw = false;
    try {
        manager.restore(id);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK_EQ(st::read_file(temp.path() / "Alice" / "a.jsonl"), alice_modified);
    CHECK_EQ(st::read_file(temp.path() / "Zed" / "z.jsonl"), zed_current);
}

TEST(backup_prune_keeps_newest_ten) {
    TempDir temp;
    st::BackupManager manager(temp.path());
    for (int i = 0; i < 12; ++i) {
        write_chat(temp.path(), "Seraphina", "chat.jsonl",
                   "{\"version\":" + std::to_string(i) + "}\n");
        manager.create("manual");
    }

    const auto entries = manager.list();
    CHECK_EQ(entries.size(), 10u);

    manager.restore(entries[0].id);
    CHECK_EQ(st::read_file(temp.path() / "Seraphina" / "chat.jsonl"),
             std::string("{\"version\":11}\n"));
}
