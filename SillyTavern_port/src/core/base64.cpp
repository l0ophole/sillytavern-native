#include "core/base64.h"

#include <cstddef>
#include <stdexcept>

namespace st {
namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decode_character(char value) {
    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }
    if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
    }
    if (value >= '0' && value <= '9') {
        return value - '0' + 52;
    }
    if (value == '+') {
        return 62;
    }
    if (value == '/') {
        return 63;
    }
    return -1;
}

[[noreturn]] void invalid(const std::string& context) {
    throw std::runtime_error(context + ": invalid base64");
}

}  // namespace

std::string base64_decode(const std::string& text, const std::string& context) {
    if (text.empty()) {
        return {};
    }
    if (text.size() % 4 != 0) {
        invalid(context);
    }

    std::size_t padding = 0;
    if (text.back() == '=') {
        ++padding;
        if (text.size() >= 2 && text[text.size() - 2] == '=') {
            ++padding;
        }
    }
    const std::size_t data_end = text.size() - padding;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '=' && i < data_end) {
            invalid(context);
        }
    }

    std::string result;
    result.reserve(text.size() / 4 * 3);

    for (std::size_t block = 0; block < text.size(); block += 4) {
        int values[4] = {0, 0, 0, 0};
        for (std::size_t offset = 0; offset < 4; ++offset) {
            const char character = text[block + offset];
            if (character == '=') {
                values[offset] = 0;
                continue;
            }
            values[offset] = decode_character(character);
            if (values[offset] < 0) {
                invalid(context);
            }
        }

        const bool last_block = block + 4 == text.size();
        if (last_block && padding == 1 && (values[2] & 0x03) != 0) {
            invalid(context);
        }
        if (last_block && padding == 2 && (values[1] & 0x0F) != 0) {
            invalid(context);
        }

        result += static_cast<char>((values[0] << 2) | (values[1] >> 4));
        if (!(last_block && padding == 2)) {
            result += static_cast<char>(((values[1] & 0x0F) << 4) | (values[2] >> 2));
        }
        if (!(last_block && padding >= 1)) {
            result += static_cast<char>(((values[2] & 0x03) << 6) | values[3]);
        }
    }
    return result;
}

std::string base64_encode(const std::string& bytes) {
    std::string result;
    result.reserve((bytes.size() + 2) / 3 * 4);

    std::size_t offset = 0;
    while (offset + 3 <= bytes.size()) {
        const unsigned int value =
            (static_cast<unsigned char>(bytes[offset]) << 16) |
            (static_cast<unsigned char>(bytes[offset + 1]) << 8) |
            static_cast<unsigned char>(bytes[offset + 2]);
        result += kAlphabet[(value >> 18) & 0x3F];
        result += kAlphabet[(value >> 12) & 0x3F];
        result += kAlphabet[(value >> 6) & 0x3F];
        result += kAlphabet[value & 0x3F];
        offset += 3;
    }

    const std::size_t remaining = bytes.size() - offset;
    if (remaining == 1) {
        const unsigned int value = static_cast<unsigned char>(bytes[offset]) << 16;
        result += kAlphabet[(value >> 18) & 0x3F];
        result += kAlphabet[(value >> 12) & 0x3F];
        result += "==";
    } else if (remaining == 2) {
        const unsigned int value =
            (static_cast<unsigned char>(bytes[offset]) << 16) |
            (static_cast<unsigned char>(bytes[offset + 1]) << 8);
        result += kAlphabet[(value >> 18) & 0x3F];
        result += kAlphabet[(value >> 12) & 0x3F];
        result += kAlphabet[(value >> 6) & 0x3F];
        result += '=';
    }
    return result;
}

}  // namespace st
