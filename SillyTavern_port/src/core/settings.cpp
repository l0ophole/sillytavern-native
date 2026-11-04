#include "core/settings.h"

#include "core/atomic_file.h"
#include "core/identity.h"
#include "core/names.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace st {
namespace {

constexpr std::size_t kMaxSettingsBytes = 1024u * 1024u;

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

void erase_known(Json& value, const char* key) {
    value.erase(key);
}

}  // namespace

Settings Settings::from_json(const Json& value) {
    if (!value.is_object()) {
        throw std::runtime_error("settings: root value must be an object");
    }

    Settings settings;
    settings.unknown = value;

    if (value.contains("api_base_url") && value["api_base_url"].is_string()) {
        settings.api_base_url = value["api_base_url"].get<std::string>();
    }
    if (value.contains("api_key") && value["api_key"].is_string()) {
        settings.api_key = value["api_key"].get<std::string>();
    }
    if (value.contains("model") && value["model"].is_string()) {
        settings.model = value["model"].get<std::string>();
    }
    if (value.contains("temperature") && value["temperature"].is_number()) {
        settings.temperature = value["temperature"].get<double>();
    }
    if (value.contains("top_p") && value["top_p"].is_number()) {
        settings.top_p = value["top_p"].get<double>();
    }
    if (value.contains("max_tokens") && value["max_tokens"].is_number_integer()) {
        settings.max_tokens = value["max_tokens"].get<int>();
    }
    if (value.contains("context_size") && value["context_size"].is_number_integer()) {
        settings.context_size = value["context_size"].get<int>();
    }
    if (value.contains("user_name") && value["user_name"].is_string()) {
        settings.user_name = value["user_name"].get<std::string>();
    }
    if (value.contains("persona_name") && value["persona_name"].is_string()) {
        settings.persona_name = value["persona_name"].get<std::string>();
    }
    if (value.contains("system_prompt") && value["system_prompt"].is_string()) {
        settings.system_prompt = value["system_prompt"].get<std::string>();
    }
    if (value.contains("stream") && value["stream"].is_boolean()) {
        settings.stream = value["stream"].get<bool>();
    }
    if (value.contains("request_timeout_seconds") &&
        value["request_timeout_seconds"].is_number_integer()) {
        settings.request_timeout_seconds = value["request_timeout_seconds"].get<int>();
    }
    if (value.contains("last_character") && value["last_character"].is_string()) {
        settings.last_character = value["last_character"].get<std::string>();
    }
    if (value.contains("last_chat") && value["last_chat"].is_string()) {
        settings.last_chat = value["last_chat"].get<std::string>();
    }
    if (value.contains("authors_note_enabled") && value["authors_note_enabled"].is_boolean()) {
        settings.authors_note_enabled = value["authors_note_enabled"].get<bool>();
    }
    if (value.contains("authors_note") && value["authors_note"].is_string()) {
        settings.authors_note = value["authors_note"].get<std::string>();
    }
    if (value.contains("authors_note_depth") && value["authors_note_depth"].is_number_integer()) {
        settings.authors_note_depth = value["authors_note_depth"].get<int>();
    }
    if (value.contains("world_info_enabled") && value["world_info_enabled"].is_boolean()) {
        settings.world_info_enabled = value["world_info_enabled"].get<bool>();
    }
    if (value.contains("world_info_book") && value["world_info_book"].is_string()) {
        settings.world_info_book = value["world_info_book"].get<std::string>();
    }

    for (const char* key : {"api_base_url", "api_key", "model", "temperature", "top_p",
                            "max_tokens", "context_size", "user_name", "persona_name", "system_prompt",
                            "stream", "request_timeout_seconds", "last_character", "last_chat",
                            "authors_note_enabled", "authors_note", "authors_note_depth",
                            "world_info_enabled", "world_info_book"}) {
        erase_known(settings.unknown, key);
    }

    settings.validate();
    return settings;
}

Settings Settings::load(const std::filesystem::path& path) {
    if (!file_exists(path)) {
        Settings settings;
        settings.validate();
        return settings;
    }
    Settings settings =
        from_json(parse_json(read_file_limited(path, kMaxSettingsBytes, "settings"), "settings"));

    std::error_code error;
    const std::filesystem::perms permissions = std::filesystem::status(path, error).permissions();
    const std::filesystem::perms shared =
        std::filesystem::perms::group_all | std::filesystem::perms::others_all;
    if (!error && (permissions & shared) != std::filesystem::perms::none) {
        std::filesystem::permissions(path,
                                     std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace,
                                     error);
    }
    return settings;
}

void Settings::save(const std::filesystem::path& path) const {
    write_file_atomic(path, dump_json(to_json(), 2) + "\n",
                      std::filesystem::perms::owner_read |
                          std::filesystem::perms::owner_write);
}

Json Settings::to_json() const {
    Json value = unknown.is_object() ? unknown : Json::object();
    value["api_base_url"] = api_base_url;
    value["api_key"] = api_key;
    value["model"] = model;
    value["temperature"] = temperature;
    value["top_p"] = top_p;
    value["max_tokens"] = max_tokens;
    value["context_size"] = context_size;
    value["user_name"] = user_name;
    value["persona_name"] = persona_name;
    value["system_prompt"] = system_prompt;
    value["stream"] = stream;
    value["request_timeout_seconds"] = request_timeout_seconds;
    value["last_character"] = last_character;
    value["last_chat"] = last_chat;
    value["authors_note_enabled"] = authors_note_enabled;
    value["authors_note"] = authors_note;
    value["authors_note_depth"] = authors_note_depth;
    value["world_info_enabled"] = world_info_enabled;
    value["world_info_book"] = world_info_book;
    return value;
}

void Settings::validate() {
    if (trim(api_base_url).empty()) {
        api_base_url = "https://api.openai.com/v1";
    }
    if (trim(model).empty()) {
        model = "gpt-4o-mini";
    }
    if (!std::isfinite(temperature)) {
        temperature = 1.0;
    }
    temperature = std::clamp(temperature, 0.0, 2.0);
    if (!std::isfinite(top_p)) {
        top_p = 1.0;
    }
    top_p = std::clamp(top_p, 0.0, 1.0);
    context_size = std::clamp(context_size, 256, 1000000);
    max_tokens = std::clamp(max_tokens, 1, context_size - 1);
    request_timeout_seconds = std::clamp(request_timeout_seconds, 5, 3600);
    user_name = normalize_user_name(user_name);
    persona_name = trim(persona_name);
    if (persona_name.size() > 256) {
        persona_name.resize(256);
        persona_name = trim(persona_name);
    }
    world_info_book = sanitize_filename(world_info_book, "lorebook");
    authors_note_depth = std::clamp(authors_note_depth, 0, 100);
    if (authors_note.size() > 4096) {
        authors_note.resize(4096);
    }
    if (trim(system_prompt).empty()) {
        system_prompt =
            "Write {{char}}'s next reply in a fictional chat between {{char}} and {{user}}.";
    }
}

}  // namespace st
