#include "core/names.h"

#include <cstddef>
#include <stdexcept>

namespace fs = std::filesystem;

namespace st {
namespace {

constexpr std::size_t kMaxFilenameBytes = 200;

bool is_trim_character(char value) {
    return value == ' ' || value == '.' || value == '_';
}

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

std::string sanitize_filename(const std::string& name, const std::string& fallback) {
    std::string replaced;
    replaced.reserve(name.size());
    for (const unsigned char value : name) {
        if (value < 0x20 || value == 0x7f) {
            continue;
        }
        if (value == '/' || value == '\\') {
            replaced += '_';
        } else {
            replaced += static_cast<char>(value);
        }
    }

    std::string collapsed;
    collapsed.reserve(replaced.size());
    for (const char value : replaced) {
        if (value == '_' && !collapsed.empty() && collapsed.back() == '_') {
            continue;
        }
        collapsed += value;
    }

    std::size_t begin = 0;
    std::size_t end = collapsed.size();
    while (begin < end && is_trim_character(collapsed[begin])) {
        ++begin;
    }
    while (end > begin && is_trim_character(collapsed[end - 1])) {
        --end;
    }
    const std::string trimmed = collapsed.substr(begin, end - begin);
    if (trimmed.empty() || trimmed == "." || trimmed == "..") {
        return fallback;
    }
    return truncate_utf8(trimmed, kMaxFilenameBytes);
}

fs::path safe_join(const fs::path& root, const fs::path& relative) {
    if (relative.is_absolute()) {
        throw std::runtime_error("unsafe path: absolute component " + relative.string());
    }

    const fs::path normalized_root = root.lexically_normal();
    const fs::path candidate = (normalized_root / relative).lexically_normal();

    if (!candidate.empty() && *candidate.begin() == "..") {
        throw std::runtime_error("unsafe path escapes root: " + candidate.string());
    }

    auto root_it = normalized_root.begin();
    auto candidate_it = candidate.begin();
    for (; root_it != normalized_root.end(); ++root_it, ++candidate_it) {
        if (root_it->empty()) {
            continue;
        }
        if (candidate_it == candidate.end() || *candidate_it != *root_it) {
            throw std::runtime_error("unsafe path escapes root: " + candidate.string());
        }
    }
    return candidate;
}

}  // namespace st
