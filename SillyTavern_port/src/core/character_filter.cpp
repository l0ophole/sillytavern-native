#include "core/character_filter.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace st {
namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool contains(const std::string& text, const std::string& needle) {
    return lowercase(text).find(needle) != std::string::npos;
}

bool matches(const CharacterEntry& entry, const std::string& needle) {
    if (contains(entry.card.name, needle) || contains(entry.card.description, needle) ||
        contains(entry.card.creator, needle)) {
        return true;
    }
    for (const std::string& tag : entry.card.tags) {
        if (contains(tag, needle)) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::vector<CharacterEntry> filter_characters(const std::vector<CharacterEntry>& entries,
                                              const std::string& query) {
    if (query.find_first_not_of(" \t\r\n") == std::string::npos) {
        return entries;
    }

    const std::string needle = lowercase(query);
    std::vector<CharacterEntry> result;
    result.reserve(entries.size());
    for (const CharacterEntry& entry : entries) {
        if (matches(entry, needle)) {
            result.push_back(entry);
        }
    }
    return result;
}

}  // namespace st
