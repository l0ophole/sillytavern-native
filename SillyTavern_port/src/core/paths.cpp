#include "core/paths.h"

#include <cstdlib>
#include <stdexcept>

namespace fs = std::filesystem;

namespace st {
namespace {

fs::path env_path(const char* name, const fs::path& fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return fs::path(value);
}

}  // namespace

Paths Paths::resolve() {
    const fs::path home = env_path("HOME", ".");
    const fs::path config_home = env_path("XDG_CONFIG_HOME", home / ".config");
    const fs::path data_home = env_path("XDG_DATA_HOME", home / ".local" / "share");
    return resolve(config_home, data_home);
}

Paths Paths::resolve(const fs::path& config_home, const fs::path& data_home) {
    Paths paths;
    paths.config_dir = config_home / "sillytavern-port";
    paths.data_dir = data_home / "sillytavern-port";
    paths.characters_dir = paths.data_dir / "characters";
    paths.chats_dir = paths.data_dir / "chats";
    paths.logs_dir = paths.data_dir / "logs";
    paths.settings_file = paths.config_dir / "settings.json";
    paths.instance_lock_file = paths.data_dir / "instance.lock";
    return paths;
}

void Paths::ensure() const {
    std::error_code error;
    for (const auto& directory : {config_dir, data_dir, characters_dir, chats_dir, logs_dir}) {
        if (!fs::create_directories(directory, error) && error) {
            throw std::runtime_error("cannot create directory " + directory.string() + ": " +
                                     error.message());
        }
    }
}

}  // namespace st
