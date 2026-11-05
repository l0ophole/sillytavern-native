#pragma once

#include "core/json.h"

#include <string>
#include <vector>

namespace st {

struct WorldInfoEntry {
    std::string key;
    std::string comment;
    std::string content;
    bool enabled = true;
    int position = 0;
    int depth = 0;
};

struct WorldInfoBook {
    std::string name;
    std::vector<WorldInfoEntry> entries;

    static WorldInfoBook from_json(const Json& value);
    Json to_json() const;
};

std::vector<WorldInfoEntry> triggered_entries(const WorldInfoBook& book,
                                               const std::vector<std::string>& texts,
                                               int max_recursion = 5);

}  // namespace st
