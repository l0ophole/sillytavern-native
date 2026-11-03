#include "core/character_card.h"

#include "core/atomic_file.h"
#include "core/base64.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace st {
namespace {

constexpr std::size_t kMaxCardBytes = 64u * 1024u * 1024u;
const char kPngSignature[] = "\x89PNG\r\n\x1a\n";

std::string read_string(const Json& object, const char* key) {
    if (!object.contains(key) || !object[key].is_string()) {
        return {};
    }
    return object[key].get<std::string>();
}

std::vector<std::string> read_string_array(const Json& object, const char* key) {
    std::vector<std::string> values;
    if (!object.contains(key) || !object[key].is_array()) {
        return values;
    }
    for (const auto& item : object[key]) {
        if (item.is_string()) {
            values.push_back(item.get<std::string>());
        }
    }
    return values;
}

std::uint32_t read_be32(const std::string& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 3]));
}

std::string trim_ascii_whitespace(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool is_png(const std::string& bytes) {
    return bytes.size() >= 8 && bytes.compare(0, 8, kPngSignature, 8) == 0;
}

CharacterCard parse_png(const std::string& bytes, const std::string& context) {
    std::size_t offset = 8;
    std::string chara_text;
    std::string ccv3_text;

    while (offset < bytes.size()) {
        if (bytes.size() - offset < 8) {
            throw std::runtime_error(context + ": truncated PNG chunk header");
        }
        const std::uint32_t length = read_be32(bytes, offset);
        const std::string type = bytes.substr(offset + 4, 4);
        offset += 8;

        if (length > bytes.size() - offset) {
            throw std::runtime_error(context + ": truncated PNG chunk data");
        }
        if (bytes.size() - offset - length < 4) {
            throw std::runtime_error(context + ": truncated PNG chunk CRC");
        }
        const std::string data = bytes.substr(offset, length);
        offset += static_cast<std::size_t>(length) + 4;

        if (type == "tEXt") {
            const std::size_t separator = data.find('\0');
            if (separator != std::string::npos) {
                const std::string keyword = data.substr(0, separator);
                const std::string text = data.substr(separator + 1);
                if (keyword == "ccv3") {
                    ccv3_text = text;
                } else if (keyword == "chara" && chara_text.empty()) {
                    chara_text = text;
                }
            }
        } else if (type == "IEND") {
            break;
        }
    }

    const std::string& selected = !ccv3_text.empty() ? ccv3_text : chara_text;
    if (selected.empty()) {
        throw std::runtime_error(context + ": no character card metadata found in PNG");
    }

    const std::string decoded = base64_decode(trim_ascii_whitespace(selected),
                                              context + ": character card metadata");
    return card_from_json(parse_json(decoded, context + ": character card metadata"));
}

}  // namespace

CharacterCard card_from_json(const Json& value) {
    if (!value.is_object()) {
        throw std::runtime_error("character card: root value must be an object");
    }

    Json data = value;
    bool versioned = false;
    if (value.contains("spec") && value["spec"].is_string()) {
        const std::string spec = value["spec"].get<std::string>();
        if (spec.rfind("chara_card_v", 0) == 0 && value.contains("data") &&
            value["data"].is_object()) {
            data = value["data"];
            versioned = true;
        }
    }

    CharacterCard card;
    card.name = read_string(data, "name");
    if (card.name.empty()) {
        throw std::runtime_error("character card: missing name");
    }

    card.description = read_string(data, "description");
    card.personality = read_string(data, "personality");
    card.scenario = read_string(data, "scenario");
    card.first_mes = read_string(data, "first_mes");
    card.mes_example = read_string(data, "mes_example");
    card.system_prompt = read_string(data, "system_prompt");
    card.post_history_instructions = read_string(data, "post_history_instructions");
    card.creator = read_string(data, "creator");
    card.character_version = read_string(data, "character_version");
    card.alternate_greetings = read_string_array(data, "alternate_greetings");

    card.spec_version = read_string(value, "spec_version");
    if (card.spec_version.empty()) {
        card.spec_version = read_string(data, "spec_version");
    }

    card.tags = read_string_array(data, "tags");
    if (card.tags.empty() && versioned) {
        card.tags = read_string_array(value, "tags");
    }

    card.extra = data;
    for (const char* key : {"name", "description", "personality", "scenario", "first_mes",
                            "mes_example", "system_prompt", "post_history_instructions",
                            "creator", "character_version", "alternate_greetings", "tags",
                            "spec_version"}) {
        card.extra.erase(key);
    }
    return card;
}

CharacterCard parse_character_card(const std::string& bytes, const std::string& context) {
    if (bytes.size() > kMaxCardBytes) {
        throw std::runtime_error(context + ": character card exceeds size limit");
    }
    if (is_png(bytes)) {
        return parse_png(bytes, context);
    }
    return card_from_json(parse_json(bytes, context));
}

CharacterCard parse_character_card_file(const std::filesystem::path& path) {
    const std::string bytes = read_file_limited(path, kMaxCardBytes, path.string());
    return parse_character_card(bytes, path.string());
}

Json card_to_json(const CharacterCard& card) {
    Json data = card.extra.is_object() ? card.extra : Json::object();
    data["name"] = card.name;
    data["description"] = card.description;
    data["personality"] = card.personality;
    data["scenario"] = card.scenario;
    data["first_mes"] = card.first_mes;
    data["mes_example"] = card.mes_example;
    data["system_prompt"] = card.system_prompt;
    data["post_history_instructions"] = card.post_history_instructions;
    data["creator"] = card.creator;
    data["character_version"] = card.character_version;
    data["alternate_greetings"] = card.alternate_greetings;
    data["tags"] = card.tags;
    if (!card.spec_version.empty()) {
        data["spec_version"] = card.spec_version;
    }

    Json root = Json::object();
    root["spec"] = "chara_card_v2";
    root["spec_version"] = card.spec_version.empty() ? "2.0" : card.spec_version;
    root["data"] = data;
    root["tags"] = card.tags;
    return root;
}

}  // namespace st
