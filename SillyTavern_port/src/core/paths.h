#pragma once

#include <filesystem>

namespace st {

struct Paths {
    std::filesystem::path config_dir;
    std::filesystem::path data_dir;
    std::filesystem::path characters_dir;
    std::filesystem::path chats_dir;
    std::filesystem::path logs_dir;
    std::filesystem::path settings_file;

    static Paths resolve();
    static Paths resolve(const std::filesystem::path& config_home,
                         const std::filesystem::path& data_home);

    void ensure() const;
};

}  // namespace st
