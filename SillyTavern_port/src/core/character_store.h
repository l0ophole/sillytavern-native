#pragma once

#include "core/character_card.h"

#include <filesystem>
#include <string>
#include <vector>

namespace st {

struct CharacterEntry {
    std::string id;
    std::filesystem::path path;
    CharacterCard card;
};

class CharacterStore {
public:
    explicit CharacterStore(std::filesystem::path characters_dir);

    std::vector<CharacterEntry> list() const;
    CharacterEntry import_file(const std::filesystem::path& source);
    void remove(const std::string& id);
    std::filesystem::path avatar_path(const CharacterEntry& entry) const;

    const std::filesystem::path& directory() const { return characters_dir_; }

private:
    std::filesystem::path characters_dir_;
};

}  // namespace st
