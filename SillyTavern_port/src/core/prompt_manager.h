#pragma once

#include "core/json.h"

#include <cstddef>
#include <string>
#include <vector>

namespace st {

struct PromptSection {
    std::string id;
    std::string title;
    std::string content;
    bool enabled = true;
    int order = 0;
};

struct PromptPreset {
    std::string name;
    std::vector<PromptSection> sections;  // sorted by order on load
    Json sampling_overrides = Json::object();  // empty = none

    static PromptPreset from_json(const Json& value);
    Json to_json() const;
};

struct PromptManager {
    std::vector<PromptSection> sections;
    std::string active_preset = "default";

    static PromptManager default_manager();
    std::vector<PromptSection> enabled_sections() const;
    void move(std::size_t from, std::size_t to);
};

}  // namespace st
