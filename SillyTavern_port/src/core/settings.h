#pragma once

#include "core/json.h"

#include <filesystem>
#include <string>

namespace st {

struct Settings {
    std::string api_base_url = "https://api.openai.com/v1";
    std::string api_key;
    std::string model = "gpt-4o-mini";
    double temperature = 1.0;
    double top_p = 1.0;
    int max_tokens = 300;
    int context_size = 4095;
    std::string user_name = "User";
    std::string system_prompt =
        "Write {{char}}'s next reply in a fictional chat between {{char}} and {{user}}.";
    bool stream = true;
    int request_timeout_seconds = 300;
    std::string last_character;
    std::string last_chat;
    bool authors_note_enabled = false;
    std::string authors_note;
    int authors_note_depth = 4;
    bool world_info_enabled = false;
    std::string world_info_book = "lorebook";
    Json unknown = Json::object();

    static Settings load(const std::filesystem::path& path);
    static Settings from_json(const Json& value);

    void save(const std::filesystem::path& path) const;
    Json to_json() const;
    void validate();
};

}  // namespace st
