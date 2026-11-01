#pragma once

#include <filesystem>
#include <string>

namespace st {

void ensure_private_file(const std::filesystem::path& path);
std::string mask_secret(const std::string& secret);
bool is_secret_redacted(const std::string& text, const std::string& secret);

}  // namespace st
