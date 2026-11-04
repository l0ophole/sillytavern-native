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

TEST(backup_create_skips_symlinks) {
    TempDir temp;
    TempDir outside;
    write_chat(temp.path(), "Seraphina", "chat.jsonl", "{\"hello\":\"real\"}\n");
    // A symlinked file inside a real character dir pointing outside.
    std::filesystem::create_symlink(outside.path() / "secret.jsonl",
                                    temp.path() / "Seraphina" / "linked.jsonl");
    // A symlinked character dir pointing outside.
    std::filesystem::create_directory_symlink(outside.path(), temp.path() / "Eve");
    // A symlink to a file outside, placed at the chats_root level as a char dir.
    write_chat(temp.path(), "Bob", "chat.jsonl", "{\"hello\":\"bob\"}\n");

    st::BackupManager manager(temp.path());
    const std::string id = manager.create("manual");

    const auto backup_root = temp.path() / ".backups" / id;
    CHECK(std::filesystem::is_regular_file(backup_root / "Seraphina" / "chat.jsonl"));
    CHECK(!std::filesystem::exists(backup_root / "Seraphina" / "linked.jsonl"));
    CHECK(!std::filesystem::exists(backup_root / "Eve"));
    CHECK(std::filesystem::is_regular_file(backup_root / "Bob" / "chat.jsonl"));
    const auto entries = manager.list();
    CHECK_EQ(entries.size(), 1u);
    CHECK_EQ(entries[0].chats, 2u);
}

TEST(backup_restore_skips_symlinks_in_backup) {
    TempDir temp;
    TempDir outside;
    write_chat(temp.path(), "Seraphina", "chat.jsonl", "{\"hello\":\"real\"}\n");
    st::BackupManager manager(temp.path());
    const std::string id = manager.create("manual");

    // Plant a symlinked chat inside the backup pointing outside; restore
    // must not read/copy it into chats_root.
    st::write_file_atomic(outside.path() / "evil.jsonl", "{\"hello\":\"outside\"}\n");
    std::filesystem::create_symlink(outside.path() / "evil.jsonl",
                                    temp.path() / ".backups" / id / "Seraphina" / "evil.jsonl");

    // Also a symlinked character dir inside the backup.
    std::filesystem::create_directory_symlink(outside.path(),
                                              temp.path() / ".backups" / id / "Mallory");

    manager.restore(id);
    CHECK(!std::filesystem::exists(temp.path() / "Seraphina" / "evil.jsonl"));
    CHECK(!std::filesystem::exists(temp.path() / "Mallory"));
    CHECK_EQ(st::read_file(temp.path() / "Seraphina" / "chat.jsonl"),
             std::string("{\"hello\":\"real\"}\n"));
}

TEST(backup_restore_refuses_symlinked_destination) {
    TempDir temp;
    TempDir outside;
    write_chat(temp.path(), "Seraphina", "chat.jsonl", "{\"hello\":\"real\"}\n");
    st::BackupManager manager(temp.path());
    const std::string id = manager.create("manual");

    // Replace the live character dir with a symlink to outside.
    std::filesystem::remove_all(temp.path() / "Seraphina");
    std::filesystem::create_directory_symlink(outside.path(), temp.path() / "Seraphina");

    bool threw = false;
    try {
        manager.restore(id);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    // Nothing was written outside.
    CHECK(!std::filesystem::exists(outside.path() / "chat.jsonl"));
}

TEST(backup_manifest_bytes_reflects_actual_copied_sizes) {
    TempDir temp;
    const std::string a = "{\"hello\":\"a-longer-chat-content\"}\n";
    const std::string b = "x";
    write_chat(temp.path(), "Seraphina", "a.jsonl", a);
    write_chat(temp.path(), "Bob", "b.jsonl", b);

    st::BackupManager manager(temp.path());
    const std::string id = manager.create("manual");

    // Independently measure the copied files' actual sizes.
    std::uintmax_t actual = 0;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(temp.path() / ".backups" / id)) {
        if (entry.is_regular_file() && entry.path().extension() == ".jsonl") {
            actual += std::filesystem::file_size(entry.path());
        }
    }
    CHECK_EQ(actual, a.size() + b.size());

    const auto entries = manager.list();
    CHECK_EQ(entries.size(), 1u);
    CHECK_EQ(entries[0].bytes, actual);
    CHECK_EQ(entries[0].chats, 2u);
}

TEST(backup_create_refuses_over_512mib) {
    TempDir temp;
    write_chat(temp.path(), "Seraphina", "big.jsonl", "x");
    // Sparse file just over 512 MiB: file_size reports the apparent size
    // without materialising 512 MiB on disk, and create must refuse.
    std::filesystem::resize_file(temp.path() / "Seraphina" / "big.jsonl",
                                 512u * 1024u * 1024u + 1u);

    st::BackupManager manager(temp.path());
    bool threw = false;
    try {
        manager.create("manual");
    } catch (const std::runtime_error& e) {
        threw = std::string(e.what()) == "backup exceeds 512 MiB limit";
    }
    CHECK(threw);
    CHECK(manager.list().empty());
    // No half-finished backup left behind.
    CHECK(!std::filesystem::exists(temp.path() / ".backups" / "something"));
    for (const auto& entry : std::filesystem::directory_iterator(temp.path() / ".backups")) {
        CHECK(entry.path().filename().string().find(".tmp") == std::string::npos);
    }
}
