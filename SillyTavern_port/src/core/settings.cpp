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
    if (value.contains("instruct_mode") && value["instruct_mode"].is_boolean()) {
        settings.instruct_mode = value["instruct_mode"].get<bool>();
    }
    if (value.contains("instruct_format") && value["instruct_format"].is_string()) {
        settings.instruct_format = value["instruct_format"].get<std::string>();
    }
    if (value.contains("context_template") && value["context_template"].is_string()) {
        settings.context_template = value["context_template"].get<std::string>();
    }
    if (value.contains("world_info_enabled") && value["world_info_enabled"].is_boolean()) {
        settings.world_info_enabled = value["world_info_enabled"].get<bool>();
    }
    if (value.contains("world_info_book") && value["world_info_book"].is_string()) {
        settings.world_info_book = value["world_info_book"].get<std::string>();
    }
    if (value.contains("active_preset") && value["active_preset"].is_string()) {
        settings.active_preset = value["active_preset"].get<std::string>();
    }
    if (value.contains("top_k") && value["top_k"].is_number_integer()) {
        settings.top_k = value["top_k"].get<int>();
    }
    if (value.contains("repetition_penalty") && value["repetition_penalty"].is_number()) {
        settings.repetition_penalty = value["repetition_penalty"].get<double>();
    }
    if (value.contains("presence_penalty") && value["presence_penalty"].is_number()) {
        settings.presence_penalty = value["presence_penalty"].get<double>();
    }
    if (value.contains("frequency_penalty") && value["frequency_penalty"].is_number()) {
        settings.frequency_penalty = value["frequency_penalty"].get<double>();
    }
    if (value.contains("dry_multiplier") && value["dry_multiplier"].is_number()) {
        settings.dry_multiplier = value["dry_multiplier"].get<double>();
    }
    if (value.contains("mirostat_tau") && value["mirostat_tau"].is_number()) {
        settings.mirostat_tau = value["mirostat_tau"].get<double>();
    }
    if (value.contains("regex_rules") && value["regex_rules"].is_array()) {
        settings.regex_rules = value["regex_rules"];
    }
    if (value.contains("variables") && value["variables"].is_object()) {
        for (const auto& [key, entry] : value["variables"].items()) {
            if (entry.is_string()) {
                settings.variables[key] = entry.get<std::string>();
            }
        }
    }

    for (const char* key : {"api_base_url", "api_key", "model", "temperature", "top_p",
                            "max_tokens", "context_size", "user_name", "persona_name", "system_prompt",
                            "stream", "request_timeout_seconds", "last_character", "last_chat",
                            "authors_note_enabled", "authors_note", "authors_note_depth",
                            "instruct_mode", "instruct_format", "context_template",
                            "world_info_enabled", "world_info_book", "active_preset",
                            "top_k", "repetition_penalty", "presence_penalty",
                            "frequency_penalty", "dry_multiplier", "mirostat_tau",
                            "regex_rules", "variables"}) {
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
    value["instruct_mode"] = instruct_mode;
    value["instruct_format"] = instruct_format;
    value["context_template"] = context_template;
    value["world_info_enabled"] = world_info_enabled;
    value["world_info_book"] = world_info_book;
    value["active_preset"] = active_preset;
    value["top_k"] = top_k;
    value["repetition_penalty"] = repetition_penalty;
    value["presence_penalty"] = presence_penalty;
    value["frequency_penalty"] = frequency_penalty;
    value["dry_multiplier"] = dry_multiplier;
    value["mirostat_tau"] = mirostat_tau;
    value["regex_rules"] = regex_rules.is_array() ? regex_rules : Json::array();
    Json variables_json = Json::object();
    for (const auto& [key, entry] : variables) {
        variables_json[key] = entry;
    }
    value["variables"] = variables_json;
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
    active_preset = sanitize_filename(active_preset, "default");
    top_k = std::clamp(top_k, 0, 1000);
    if (!std::isfinite(repetition_penalty)) {
        repetition_penalty = 1.0;
    }
    repetition_penalty = std::clamp(repetition_penalty, 0.5, 2.0);
    if (!std::isfinite(presence_penalty)) {
        presence_penalty = 0.0;
    }
    presence_penalty = std::clamp(presence_penalty, -2.0, 2.0);
    if (!std::isfinite(frequency_penalty)) {
        frequency_penalty = 0.0;
    }
    frequency_penalty = std::clamp(frequency_penalty, -2.0, 2.0);
    if (!std::isfinite(dry_multiplier)) {
        dry_multiplier = 0.0;
    }
    dry_multiplier = std::clamp(dry_multiplier, 0.0, 10.0);
    if (!std::isfinite(mirostat_tau)) {
        mirostat_tau = 5.0;
    }
    mirostat_tau = std::clamp(mirostat_tau, 0.0, 20.0);
    if (!regex_rules.is_array()) {
        regex_rules = Json::array();
    }
    authors_note_depth = std::clamp(authors_note_depth, 0, 100);
    if (authors_note.size() > 4096) {
        authors_note.resize(4096);
    }
    if (instruct_format != "plain" && instruct_format != "chatml" && instruct_format != "llama") {
        instruct_format = "plain";
    }
    if (context_template.size() > 8192) {
        context_template.resize(8192);
    }
    if (trim(system_prompt).empty()) {
        system_prompt =
            "Write {{char}}'s next reply in a fictional chat between {{char}} and {{user}}.";
    }
}

}  // namespace st
