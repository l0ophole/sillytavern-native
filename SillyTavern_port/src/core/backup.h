#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace st {

struct BackupEntry {
    std::string id;
    std::filesystem::path path;
    std::size_t chats = 0;
    std::uintmax_t bytes = 0;
    std::string created;
};

class BackupManager {
public:
    explicit BackupManager(std::filesystem::path chats_root);
    std::string create(const std::string& label = {});
    std::vector<BackupEntry> list() const;
    void restore(const std::string& id) const;
    void prune(std::size_t keep = 10);

private:
    std::filesystem::path backups_dir() const;
    std::filesystem::path chats_root_;
};

}  // namespace st
