#pragma once

#include "core/json.h"

#include <filesystem>
#include <string>
#include <vector>

namespace st {

struct Persona {
    std::string name;
    std::string description;
    std::string header = "";  // optional opening line
};

struct PersonaStore {
    std::vector<Persona> personas;
    std::string active_name;  // empty = built-in User

    static PersonaStore from_json(const Json& value);
    Json to_json() const;
    const Persona* active() const;

    // Loads personas.json (1 MiB cap). Missing file yields an empty store;
    // read/parse errors throw.
    static PersonaStore load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};

}  // namespace st
