#include "core/json.h"

#include <stdexcept>

namespace st {

Json parse_json(const std::string& text, const std::string& context) {
    try {
        return Json::parse(text);
    } catch (const std::exception& error) {
        throw std::runtime_error(context + ": " + error.what());
    }
}

std::string dump_json(const Json& value, int indent) {
    return value.dump(indent);
}

}  // namespace st
