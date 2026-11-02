#include "core/identity.h"

#include <cstddef>

namespace st {
namespace {

constexpr std::size_t kMaxUserNameBytes = 64;

std::string truncate_utf8(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) {
        return text;
    }
    std::size_t end = limit;
    std::size_t start = end;
    while (start > 0 && (static_cast<unsigned char>(text[start - 1]) & 0xC0) == 0x80) {
        --start;
    }
    if (start > 0) {
        const unsigned char lead = static_cast<unsigned char>(text[start - 1]);
        std::size_t needed = 1;
        if ((lead & 0xE0) == 0xC0) {
            needed = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            needed = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            needed = 4;
        }
        if (start - 1 + needed > end) {
            end = start - 1;
        }
    }
    return text.substr(0, end);
}

}  // namespace

std::string normalize_user_name(const std::string& name) {
    std::string cleaned;
    cleaned.reserve(name.size());
    for (const unsigned char value : name) {
        if (value < 0x20 || value == 0x7f) {
            continue;
        }
        if (value == ' ' && !cleaned.empty() && cleaned.back() == ' ') {
            continue;
        }
        cleaned += static_cast<char>(value);
    }

    const auto first = cleaned.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "User";
    }
    const auto last = cleaned.find_last_not_of(' ');
    std::string result = cleaned.substr(first, last - first + 1);
    if (result.empty()) {
        return "User";
    }
    return truncate_utf8(result, kMaxUserNameBytes);
}

}  // namespace st
