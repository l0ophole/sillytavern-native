#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace st {

using Json = nlohmann::json;

Json parse_json(const std::string& text, const std::string& context);
std::string dump_json(const Json& value, int indent = -1);

}  // namespace st
