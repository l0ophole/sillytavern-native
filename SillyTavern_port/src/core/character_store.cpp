#include "core/character_store.h"

#include "core/atomic_file.h"
#include "core/names.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace st {
namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool is_card_extension(const std::string& extension) {
    return extension == ".png" || extension == ".json";
}

bool looks_like_png(const std::string& bytes) {
    static const char signature[] = "\x89PNG\r\n\x1a\n";
    return bytes.size() >= 8 && std::equal(signature, signature + 8, bytes.begin());
}

}  // namespace

CharacterStore::CharacterStore(fs::path characters_dir) : characters_dir_(std::move(characters_dir)) {}

std::vector<CharacterEntry> CharacterStore::list() const {
    std::vector<CharacterEntry> entries;
    std::error_code error;
    if (!fs::is_directory(characters_dir_, error)) {
        std::error_code exist_error;
        if (fs::exists(characters_dir_, exist_error)) {
            throw std::runtime_error("character directory is not a directory: " +
                                     characters_dir_.string());
        }
        return entries;
    }

    for (const auto& item : fs::directory_iterator(characters_dir_, error)) {
        if (error) {
            break;
        }
        if (!item.is_regular_file()) {
            continue;
        }
        const std::string extension = lowercase(item.path().extension().string());
        if (!is_card_extension(extension)) {
            continue;
        }
        try {
            CharacterCard card = parse_character_card_file(item.path());
            entries.push_back(CharacterEntry{item.path().stem().string(), item.path(), std::move(card)});
        } catch (const std::exception&) {
            // Skip unreadable or malformed cards; import reports errors to the user.
        }
    }

    std::sort(entries.begin(), entries.end(), [](const CharacterEntry& left, const CharacterEntry& right) {
        if (left.card.name != right.card.name) {
            return left.card.name < right.card.name;
        }
        return left.id < right.id;
    });
    return entries;
}

CharacterEntry CharacterStore::import_file(const fs::path& source) {
    constexpr std::size_t kMaxCardBytes = 64u * 1024u * 1024u;
    const std::string bytes = read_file_limited(source, kMaxCardBytes, "character card");
    CharacterCard card = parse_character_card(bytes, source.string());

    std::string extension = lowercase(source.extension().string());
    if (!is_card_extension(extension)) {
        extension = looks_like_png(bytes) ? ".png" : ".json";
    }

    const std::string base = sanitize_filename(card.name, "character");
    std::string id = base;
    fs::path target = safe_join(characters_dir_, id + extension);
    int suffix = 2;
    while (fs::exists(target)) {
        id = base + "-" + std::to_string(suffix);
        ++suffix;
        target = safe_join(characters_dir_, id + extension);
    }

    write_file_atomic(target, bytes);
    return CharacterEntry{id, target, std::move(card)};
}

void CharacterStore::remove(const std::string& id) {
    const std::string base = sanitize_filename(id, "character");
    for (const char* extension : {".png", ".json"}) {
        const fs::path target = safe_join(characters_dir_, base + extension);
        std::error_code error;
        fs::remove(target, error);
    }
}

fs::path CharacterStore::avatar_path(const CharacterEntry& entry) const {
    if (lowercase(entry.path.extension().string()) == ".png") {
        return entry.path;
    }
    return {};
}

}  // namespace st
