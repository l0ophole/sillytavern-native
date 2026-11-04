#include "core/persona.h"

#include "core/atomic_file.h"

#include <string>
#include <utility>
#include <vector>

namespace st {
namespace {

constexpr std::size_t kMaxPersonasBytes = 1024u * 1024u;

std::string read_string_field(const Json& value, const char* key) {
    if (value.contains(key) && value[key].is_string()) {
        return value[key].get<std::string>();
    }
    return {};
}

}  // namespace

PersonaStore PersonaStore::from_json(const Json& value) {
    PersonaStore store;
    if (!value.is_object()) {
        return store;
    }
    store.active_name = read_string_field(value, "active");
    if (!value.contains("personas") || !value["personas"].is_array()) {
        return store;
    }
    for (const Json& item : value["personas"]) {
        if (!item.is_object()) {
            continue;
        }
        Persona persona;
        persona.name = read_string_field(item, "name");
        if (persona.name.empty()) {
            continue;
        }
        persona.description = read_string_field(item, "description");
        persona.header = read_string_field(item, "header");
        store.personas.push_back(std::move(persona));
    }
    return store;
}

Json PersonaStore::to_json() const {
    Json personas = Json::array();
    for (const Persona& persona : this->personas) {
        Json item = Json::object();
        item["name"] = persona.name;
        item["description"] = persona.description;
        item["header"] = persona.header;
        personas.push_back(std::move(item));
    }
    Json value = Json::object();
    value["personas"] = std::move(personas);
    value["active"] = active_name;
    return value;
}

const Persona* PersonaStore::active() const {
    if (active_name.empty()) {
        return nullptr;
    }
    for (const Persona& persona : personas) {
        if (persona.name == active_name) {
            return &persona;
        }
    }
    return nullptr;
}

PersonaStore PersonaStore::load(const std::filesystem::path& path) {
    if (!file_exists(path)) {
        return PersonaStore{};
    }
    const std::string text = read_file_limited(path, kMaxPersonasBytes, "personas");
    return from_json(parse_json(text, "personas"));
}

void PersonaStore::save(const std::filesystem::path& path) const {
    write_file_atomic(path, dump_json(to_json(), 2) + "\n",
                      std::filesystem::perms::owner_read |
                          std::filesystem::perms::owner_write);
}

}  // namespace st
