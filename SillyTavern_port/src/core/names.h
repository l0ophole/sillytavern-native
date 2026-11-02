#pragma once

#include <filesystem>
#include <string>

namespace st {

std::string sanitize_filename(const std::string& name, const std::string& fallback = "unnamed");

std::filesystem::path safe_join(const std::filesystem::path& root,
                                const std::filesystem::path& relative);

}  // namespace st
