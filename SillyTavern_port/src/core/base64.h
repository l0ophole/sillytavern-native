#pragma once

#include <string>

namespace st {

std::string base64_decode(const std::string& text, const std::string& context);
std::string base64_encode(const std::string& bytes);

}  // namespace st
