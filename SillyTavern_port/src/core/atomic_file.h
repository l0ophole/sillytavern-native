#pragma once

#include <filesystem>
#include <string>

namespace st {

std::string read_file(const std::filesystem::path& path);
bool file_exists(const std::filesystem::path& path);

void write_file_atomic(const std::filesystem::path& path,
                       const std::string& data,
                       std::filesystem::perms permissions =
                           std::filesystem::perms::owner_read |
                           std::filesystem::perms::owner_write |
                           std::filesystem::perms::group_read |
                           std::filesystem::perms::others_read);

}  // namespace st
