#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace png_fixture {

inline void append_u32(std::string& out, std::uint32_t value) {
    out += static_cast<char>((value >> 24) & 0xFF);
    out += static_cast<char>((value >> 16) & 0xFF);
    out += static_cast<char>((value >> 8) & 0xFF);
    out += static_cast<char>(value & 0xFF);
}

inline void append_chunk(std::string& out, const std::string& type, const std::string& data) {
    append_u32(out, static_cast<std::uint32_t>(data.size()));
    out += type;
    out += data;
    append_u32(out, 0);  // CRC is not validated by the parser.
}

inline std::string build_png(const std::vector<std::pair<std::string, std::string>>& texts) {
    std::string out("\x89PNG\r\n\x1a\n", 8);

    std::string ihdr;
    append_u32(ihdr, 1);
    append_u32(ihdr, 1);
    ihdr += static_cast<char>(8);  // bit depth
    ihdr += static_cast<char>(2);  // color type: truecolor
    ihdr += static_cast<char>(0);  // compression
    ihdr += static_cast<char>(0);  // filter
    ihdr += static_cast<char>(0);  // interlace
    append_chunk(out, "IHDR", ihdr);

    for (const auto& text : texts) {
        std::string data = text.first;
        data += '\0';
        data += text.second;
        append_chunk(out, "tEXt", data);
    }

    append_chunk(out, "IEND", {});
    return out;
}

}  // namespace png_fixture
