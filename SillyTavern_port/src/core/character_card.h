#pragma once

#include "core/json.h"

#include <filesystem>
#include <string>
#include <vector>

namespace st {

struct CharacterCard {
    std::string name;
    std::string description;
    std::string personality;
    std::string scenario;
    std::string first_mes;
    std::string mes_example;
    std::string system_prompt;
    std::string post_history_instructions;
    std::string creator;
    std::string character_version;
    std::string spec_version;
    std::vector<std::string> alternate_greetings;
    std::vector<std::string> tags;
    Json extra = Json::object();
};

CharacterCard parse_character_card(const std::string& bytes, const std::string& context);
CharacterCard parse_character_card_file(const std::filesystem::path& path);
CharacterCard card_from_json(const Json& value);
Json card_to_json(const CharacterCard& card);

}  // namespace st
