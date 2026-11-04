#include "core/backup.h"

#include "core/atomic_file.h"
#include "core/json.h"
#include "core/names.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;

namespace st {
namespace {

constexpr std::size_t kMaxChatBytes = 64u * 1024u * 1024u;
constexpr std::uintmax_t kMaxBackupBytes = 512u * 1024u * 1024u;

std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc {};
    gmtime_r(&seconds, &utc);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M-%S", &utc);
    return buffer;
}

bool is_skipped_root(const std::string& name) {
    return name == ".backups" || name == ".journal" || name == ".locks";
}

bool has_tmp_suffix(const std::string& name) {
    return name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0;
}

std::string padded_counter(int counter) {
    std::ostringstream stream;
    stream << '-' << std::setw(3) << std::setfill('0') << counter;
    return stream.str();
}

struct ChatFile {
    std::string character;
    std::string filename;
    fs::path source;
    std::uintmax_t bytes = 0;
};

std::vector<ChatFile> collect_chats(const fs::path& root) {
    std::vector<ChatFile> files;
    std::error_code error;
    if (!fs::is_directory(root, error)) {
        return files;
    }

    std::vector<fs::path> characters;
    for (const auto& item : fs::directory_iterator(root, error)) {
        if (error) {
            break;
        }
        std::error_code type_error;
        if (!item.is_directory(type_error) || type_error) {
            continue;
        }
        const std::string name = item.path().filename().string();
        if (is_skipped_root(name)) {
            continue;
        }
        characters.push_back(item.path());
    }
    std::sort(characters.begin(), characters.end());

    for (const auto& character_dir : characters) {
        std::vector<fs::path> chats;
        std::error_code iter_error;
        for (const auto& item : fs::directory_iterator(character_dir, iter_error)) {
            if (iter_error) {
                break;
            }
            std::error_code type_error;
            if (!item.is_regular_file(type_error) || type_error) {
                continue;
            }
            if (item.path().extension() != ".jsonl") {
                continue;
            }
            chats.push_back(item.path());
        }
        std::sort(chats.begin(), chats.end());
        for (const auto& chat_path : chats) {
            std::error_code size_error;
            const std::uintmax_t size = fs::file_size(chat_path, size_error);
            if (size_error) {
                throw std::runtime_error("cannot stat chat " + chat_path.string() + ": " +
                                         size_error.message());
            }
            files.push_back(ChatFile{character_dir.filename().string(),
                                     chat_path.filename().string(), chat_path, size});
        }
    }
    return files;
}

std::string manifest_string(const Json& manifest, const char* key, const std::string& fallback) {
    if (manifest.contains(key) && manifest[key].is_string()) {
        return manifest[key].get<std::string>();
    }
    return fallback;
}

std::uintmax_t manifest_bytes(const Json& manifest, const char* key) {
    if (manifest.contains(key) && manifest[key].is_number_unsigned()) {
        return manifest[key].get<std::uintmax_t>();
    }
    if (manifest.contains(key) && manifest[key].is_number_integer() &&
        manifest[key].get<long long>() >= 0) {
        return static_cast<std::uintmax_t>(manifest[key].get<long long>());
    }
    return 0;
}

}  // namespace

BackupManager::BackupManager(fs::path chats_root) : chats_root_(std::move(chats_root)) {}

fs::path BackupManager::backups_dir() const {
    return chats_root_ / ".backups";
}

std::string BackupManager::create(const std::string& label) {
    // Sanitize the label so it can never escape .backups: sanitize_filename
    // replaces '/' and '\\' with '_', trims leading/trailing '.'/'_'/' ',
    // drops control characters, and maps an all-unsafe label such as ".."
    // to the "unnamed" fallback. The result is always a single path
    // component, so the backup directory stays directly under .backups.
    // An empty label yields no suffix.
    std::string suffix;
    if (!label.empty()) {
        const std::string sanitized = sanitize_filename(label);
        if (!sanitized.empty()) {
            suffix = "-" + sanitized;
        }
    }

    const std::string created = utc_timestamp();
    const std::string base_id = created + suffix;

    const fs::path backups = backups_dir();
    std::error_code dir_error;
    fs::create_directories(backups, dir_error);
    if (dir_error) {
        throw std::runtime_error("cannot create backup directory " + backups.string() + ": " +
                                 dir_error.message());
    }

    std::string id = base_id;
    for (int counter = 2;; ++counter) {
        std::error_code exists_error;
        if (!fs::exists(backups / id, exists_error) &&
            !fs::exists(backups / (id + ".tmp"), exists_error)) {
            break;
        }
        id = base_id + padded_counter(counter);
    }

    const std::vector<ChatFile> files = collect_chats(chats_root_);

    std::uintmax_t total = 0;
    for (const auto& file : files) {
        total += file.bytes;
        if (total > kMaxBackupBytes) {
            throw std::runtime_error("backup exceeds 512 MiB limit");
        }
    }

    const fs::path staging = backups / (id + ".tmp");
    const fs::path final_dir = backups / id;

    std::error_code remove_error;
    fs::remove_all(staging, remove_error);
    std::error_code create_error;
    fs::create_directories(staging, create_error);
    if (create_error) {
        throw std::runtime_error("cannot create backup staging directory " + staging.string() +
                                 ": " + create_error.message());
    }

    try {
        for (const auto& file : files) {
            const fs::path destination = staging / file.character / file.filename;
            std::error_code parent_error;
            fs::create_directories(destination.parent_path(), parent_error);
            if (parent_error) {
                throw std::runtime_error("cannot create backup directory " +
                                         destination.parent_path().string() + ": " +
                                         parent_error.message());
            }
            std::error_code copy_error;
            fs::copy_file(file.source, destination, copy_error);
            if (copy_error) {
                throw std::runtime_error("cannot copy chat " + file.source.string() + ": " +
                                         copy_error.message());
            }
        }

        Json manifest = Json::object();
        manifest["id"] = id;
        manifest["created"] = created;
        manifest["chats"] = files.size();
        manifest["bytes"] = total;
        write_file_atomic(staging / "manifest.json", dump_json(manifest, 2) + "\n");

        std::error_code rename_error;
        fs::rename(staging, final_dir, rename_error);
        if (rename_error) {
            throw std::runtime_error("cannot finalize backup " + final_dir.string() + ": " +
                                     rename_error.message());
        }
    } catch (...) {
        std::error_code cleanup_error;
        fs::remove_all(staging, cleanup_error);
        throw;
    }

    prune(10);
    return id;
}

std::vector<BackupEntry> BackupManager::list() const {
    struct TimedEntry {
        BackupEntry entry;
        fs::file_time_type modified{};
    };

    std::vector<TimedEntry> timed;
    const fs::path backups = backups_dir();
    std::error_code error;
    if (!fs::is_directory(backups, error)) {
        return {};
    }

    for (const auto& item : fs::directory_iterator(backups, error)) {
        if (error) {
            break;
        }
        std::error_code type_error;
        if (!item.is_directory(type_error) || type_error) {
            continue;
        }
        const std::string name = item.path().filename().string();
        if (has_tmp_suffix(name)) {
            continue;
        }

        const fs::path manifest_path = item.path() / "manifest.json";
        std::string text;
        try {
            text = read_file_limited(manifest_path, kMaxChatBytes, "backup manifest");
        } catch (const std::runtime_error&) {
            continue;
        }
        Json manifest;
        try {
            manifest = parse_json(text, "backup manifest " + name);
        } catch (const std::runtime_error&) {
            continue;
        }
        if (!manifest.is_object()) {
            continue;
        }

        BackupEntry entry;
        entry.id = manifest_string(manifest, "id", name);
        entry.path = item.path();
        entry.chats = static_cast<std::size_t>(manifest_bytes(manifest, "chats"));
        entry.bytes = manifest_bytes(manifest, "bytes");
        entry.created = manifest_string(manifest, "created", "");

        TimedEntry timed_entry;
        timed_entry.entry = std::move(entry);
        std::error_code time_error;
        const auto modified = fs::last_write_time(item.path(), time_error);
        if (!time_error) {
            timed_entry.modified = modified;
        }
        timed.push_back(std::move(timed_entry));
    }

    std::sort(timed.begin(), timed.end(), [](const TimedEntry& a, const TimedEntry& b) {
        if (a.entry.created != b.entry.created) {
            return a.entry.created > b.entry.created;
        }
        if (a.modified != b.modified) {
            return a.modified > b.modified;
        }
        return a.entry.id > b.entry.id;
    });

    std::vector<BackupEntry> entries;
    entries.reserve(timed.size());
    for (auto& item : timed) {
        entries.push_back(std::move(item.entry));
    }
    return entries;
}

void BackupManager::restore(const std::string& id) const {
    if (id.empty() || id == "." || id == ".." || id.find('/') != std::string::npos ||
        id.find('\\') != std::string::npos || fs::path(id).is_absolute() ||
        fs::path(id).has_parent_path()) {
        throw std::runtime_error("unsafe backup id: " + id);
    }

    const fs::path backups = backups_dir();
    const fs::path backup = safe_join(backups, id);
    if (backup.parent_path() != backups.lexically_normal()) {
        throw std::runtime_error("unsafe backup id: " + id);
    }

    std::error_code error;
    if (!fs::is_directory(backup, error)) {
        throw std::runtime_error("backup not found: " + id);
    }
    if (!fs::is_regular_file(backup / "manifest.json", error)) {
        throw std::runtime_error("backup not found: " + id);
    }

    // Validate first: enumerate every backed-up chat and read each one fully
    // (64 MiB cap) before writing anything, so any failure leaves every
    // existing chat byte-identical.
    struct PendingWrite {
        fs::path destination;
        std::string content;
    };
    std::vector<PendingWrite> pending;

    std::vector<fs::path> characters;
    for (const auto& item : fs::directory_iterator(backup, error)) {
        if (error) {
            break;
        }
        if (!item.is_directory()) {
            continue;
        }
        characters.push_back(item.path());
    }
    if (error) {
        throw std::runtime_error("cannot read backup " + id + ": " + error.message());
    }
    std::sort(characters.begin(), characters.end());

    for (const auto& character_dir : characters) {
        const std::string character = character_dir.filename().string();
        std::vector<fs::path> chats;
        std::error_code iter_error;
        for (const auto& item : fs::directory_iterator(character_dir, iter_error)) {
            if (iter_error) {
                break;
            }
            if (item.path().extension() == ".jsonl") {
                chats.push_back(item.path());
            }
        }
        if (iter_error) {
            throw std::runtime_error("cannot read backup " + id + ": " + iter_error.message());
        }
        std::sort(chats.begin(), chats.end());
        for (const auto& chat_path : chats) {
            std::error_code type_error;
            if (!fs::is_regular_file(chat_path, type_error) || type_error) {
                throw std::runtime_error("backup chat is not a regular file: " + chat_path.string());
            }
            const std::string filename = chat_path.filename().string();
            // safe_join validates that the destination stays under chats_root_.
            const fs::path destination = safe_join(chats_root_, fs::path(character) / filename);
            std::string content = read_file_limited(chat_path, kMaxChatBytes, filename);
            pending.push_back(PendingWrite{destination, std::move(content)});
        }
    }

    for (const auto& write : pending) {
        write_file_atomic(write.destination, write.content);
    }
}

void BackupManager::prune(std::size_t keep) {
    const std::vector<BackupEntry> entries = list();
    if (entries.size() <= keep) {
        return;
    }
    for (std::size_t index = keep; index < entries.size(); ++index) {
        std::error_code error;
        fs::remove_all(entries[index].path, error);
    }
}

}  // namespace st
