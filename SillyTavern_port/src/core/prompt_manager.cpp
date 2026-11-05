#include "core/prompt_manager.h"

#include <algorithm>
#include <utility>

namespace st {
namespace {

std::string read_string_field(const Json& value, const char* key) {
    if (value.contains(key) && value[key].is_string()) {
        return value[key].get<std::string>();
    }
    return {};
}

bool read_bool_field(const Json& value, const char* key, bool fallback) {
    if (value.contains(key) && value[key].is_boolean()) {
        return value[key].get<bool>();
    }
    return fallback;
}

int read_int_field(const Json& value, const char* key, int fallback) {
    if (value.contains(key) && value[key].is_number_integer()) {
        return value[key].get<int>();
    }
    return fallback;
}

}  // namespace

PromptPreset PromptPreset::from_json(const Json& value) {
    PromptPreset preset;
    if (!value.is_object()) {
        return preset;
    }
    preset.name = read_string_field(value, "name");
    if (value.contains("sampling") && value["sampling"].is_object()) {
        preset.sampling_overrides = value["sampling"];
    } else if (value.contains("sampling") && value["sampling"].is_null()) {
        preset.sampling_overrides = Json::object();
    }
    if (!value.contains("sections") || !value["sections"].is_array()) {
        return preset;
    }
    for (const Json& item : value["sections"]) {
        if (!item.is_object()) {
            continue;
        }
        PromptSection section;
        section.id = read_string_field(item, "id");
        if (section.id.empty()) {
            continue;
        }
        section.title = read_string_field(item, "title");
        section.content = read_string_field(item, "content");
        section.enabled = read_bool_field(item, "enabled", true);
        section.order = read_int_field(item, "order", 0);
        preset.sections.push_back(std::move(section));
    }
    std::stable_sort(preset.sections.begin(), preset.sections.end(),
                     [](const PromptSection& a, const PromptSection& b) {
                         return a.order < b.order;
                     });
    return preset;
}

Json PromptPreset::to_json() const {
    Json section_array = Json::array();
    for (const PromptSection& section : sections) {
        Json item = Json::object();
        item["id"] = section.id;
        item["title"] = section.title;
        item["content"] = section.content;
        item["enabled"] = section.enabled;
        item["order"] = section.order;
        section_array.push_back(std::move(item));
    }
    Json value = Json::object();
    value["name"] = name;
    value["sections"] = std::move(section_array);
    value["sampling"] = sampling_overrides.is_object() ? sampling_overrides : Json::object();
    return value;
}

PromptManager PromptManager::default_manager() {
    PromptManager manager;
    manager.active_preset = "default";
    // Order matches the legacy (manager-less) prompt builder, which emits
    // the persona description before world-info lore.
    const char* ids[] = {"system",       "description", "personality", "scenario", "examples",
                         "persona",      "world_info",  "authors_note", "post_history"};
    const char* titles[] = {"System",     "Description", "Personality", "Scenario", "Examples",
                            "Persona",    "World Info",  "Author's Note", "Post History"};
    for (int i = 0; i < 9; ++i) {
        PromptSection section;
        section.id = ids[i];
        section.title = titles[i];
        section.content = "";
        section.enabled = true;
        section.order = i;
        manager.sections.push_back(std::move(section));
    }
    return manager;
}

std::vector<PromptSection> PromptManager::enabled_sections() const {
    std::vector<PromptSection> result;
    for (const PromptSection& section : sections) {
        if (section.enabled) {
            result.push_back(section);
        }
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const PromptSection& a, const PromptSection& b) {
                         return a.order < b.order;
                     });
    return result;
}

void PromptManager::move(std::size_t from, std::size_t to) {
    if (from >= sections.size() || to >= sections.size()) {
        return;
    }
    if (from == to) {
        return;
    }
    PromptSection moved = sections[from];
    sections.erase(sections.begin() + static_cast<std::ptrdiff_t>(from));
    sections.insert(sections.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    for (std::size_t i = 0; i < sections.size(); ++i) {
        sections[i].order = static_cast<int>(i);
    }
}

}  // namespace st
