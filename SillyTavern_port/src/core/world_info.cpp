#include "core/world_info.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace st {
namespace {

constexpr std::size_t kMaxEntries = 32;
constexpr std::size_t kMaxEntryBytes = 4096;
constexpr std::size_t kMaxTotalBytes = 32768;
constexpr int kMaxRecursion = 5;

std::string lower_cased(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (const unsigned char value : text) {
        if (value >= 'A' && value <= 'Z') {
            result += static_cast<char>(value - 'A' + 'a');
        } else {
            result += static_cast<char>(value);
        }
    }
    return result;
}

std::string trim_text(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::vector<std::string> split_keys(const std::string& key_field) {
    std::vector<std::string> keys;
    std::size_t start = 0;
    while (start <= key_field.size()) {
        const std::size_t comma = key_field.find(',', start);
        const std::size_t end = (comma == std::string::npos) ? key_field.size() : comma;
        std::string token = trim_text(key_field.substr(start, end - start));
        if (!token.empty()) {
            keys.push_back(lower_cased(token));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return keys;
}

bool matches_any_key(const WorldInfoEntry& entry,
                     const std::vector<std::string>& lowered_texts) {
    const std::vector<std::string> keys = split_keys(entry.key);
    for (const std::string& lowered : lowered_texts) {
        for (const std::string& key : keys) {
            if (!key.empty() && lowered.find(key) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> lower_all(const std::vector<std::string>& texts) {
    std::vector<std::string> lowered;
    lowered.reserve(texts.size());
    for (const std::string& text : texts) {
        lowered.push_back(lower_cased(text));
    }
    return lowered;
}

WorldInfoEntry parse_entry(const Json& value) {
    WorldInfoEntry entry;
    entry.key = value["key"].get<std::string>();
    entry.content = value["content"].get<std::string>();
    if (value.contains("comment") && value["comment"].is_string()) {
        entry.comment = value["comment"].get<std::string>();
    }
    if (value.contains("disable") && value["disable"].is_boolean()) {
        entry.enabled = !value["disable"].get<bool>();
    } else if (value.contains("enabled") && value["enabled"].is_boolean()) {
        entry.enabled = value["enabled"].get<bool>();
    } else {
        entry.enabled = true;
    }
    if (value.contains("position") && value["position"].is_number_integer()) {
        entry.position = value["position"].get<int>();
    }
    if (value.contains("depth") && value["depth"].is_number_integer()) {
        entry.depth = std::clamp(value["depth"].get<int>(), 0, 100);
    }
    return entry;
}

}  // namespace

WorldInfoBook WorldInfoBook::from_json(const Json& value) {
    WorldInfoBook book;
    const Json* entries_value = nullptr;
    if (value.is_array()) {
        entries_value = &value;
    } else if (value.is_object()) {
        if (value.contains("name") && value["name"].is_string()) {
            book.name = value["name"].get<std::string>();
        }
        if (value.contains("entries") && value["entries"].is_array()) {
            entries_value = &value["entries"];
        }
    } else {
        return book;
    }
    if (entries_value == nullptr) {
        return book;
    }
    for (const Json& item : *entries_value) {
        if (!item.is_object()) {
            continue;
        }
        if (!item.contains("key") || !item.contains("content")) {
            continue;
        }
        if (!item["key"].is_string() || !item["content"].is_string()) {
            continue;
        }
        if (trim_text(item["key"].get<std::string>()).empty() ||
            trim_text(item["content"].get<std::string>()).empty()) {
            continue;
        }
        book.entries.push_back(parse_entry(item));
    }
    return book;
}

Json WorldInfoBook::to_json() const {
    Json value = Json::object();
    value["name"] = name;
    Json entries = Json::array();
    for (const WorldInfoEntry& entry : this->entries) {
        Json item = Json::object();
        item["key"] = entry.key;
        item["comment"] = entry.comment;
        item["content"] = entry.content;
        item["disable"] = !entry.enabled;
        item["position"] = entry.position;
        item["depth"] = std::clamp(entry.depth, 0, 100);
        entries.push_back(std::move(item));
    }
    value["entries"] = std::move(entries);
    return value;
}

std::vector<WorldInfoEntry> triggered_entries(const WorldInfoBook& book,
                                               const std::vector<std::string>& texts,
                                               int max_recursion) {
    const int recursion = std::clamp(max_recursion, 0, kMaxRecursion);
    std::vector<WorldInfoEntry> collected;
    std::set<std::pair<std::string, std::string>> seen;
    std::vector<std::string> current = texts;
    const int rounds = 1 + recursion;
    for (int round = 0; round < rounds; ++round) {
        if (current.empty()) {
            break;
        }
        const std::vector<std::string> lowered = lower_all(current);
        std::vector<WorldInfoEntry> fresh;
        for (const WorldInfoEntry& entry : book.entries) {
            if (!entry.enabled) {
                continue;
            }
            if (!matches_any_key(entry, lowered)) {
                continue;
            }
            const std::pair<std::string, std::string> identity{entry.key, entry.content};
            if (seen.count(identity) == 0) {
                seen.insert(identity);
                fresh.push_back(entry);
            }
        }
        if (fresh.empty()) {
            break;
        }
        for (const WorldInfoEntry& entry : fresh) {
            collected.push_back(entry);
        }
        current.clear();
        current.reserve(fresh.size());
        for (const WorldInfoEntry& entry : fresh) {
            current.push_back(entry.content);
        }
    }
    std::sort(collected.begin(), collected.end(),
              [](const WorldInfoEntry& lhs, const WorldInfoEntry& rhs) {
                  if (lhs.position != rhs.position) {
                      return lhs.position < rhs.position;
                  }
                  return lhs.key < rhs.key;
              });
    std::vector<WorldInfoEntry> result;
    std::size_t total_bytes = 0;
    for (const WorldInfoEntry& entry : collected) {
        if (result.size() >= kMaxEntries) {
            break;
        }
        WorldInfoEntry copy = entry;
        if (copy.content.size() > kMaxEntryBytes) {
            copy.content.resize(kMaxEntryBytes);
        }
        if (total_bytes + copy.content.size() > kMaxTotalBytes) {
            break;
        }
        total_bytes += copy.content.size();
        result.push_back(std::move(copy));
    }
    return result;
}

}  // namespace st
