#include "png_fixture.h"
#include "test_util.h"

#include "core/base64.h"
#include "core/character_card.h"
#include "core/json.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

st::CharacterCard sample_card() {
    st::CharacterCard card;
    card.name = "Seraphina";
    card.description = "A dragon.";
    card.personality = "Calm";
    card.first_mes = "Hello.";
    card.spec_version = "2.0";
    card.alternate_greetings = {"Hi."};
    card.tags = {"fantasy"};
    card.extra["future"] = true;
    return card;
}

std::string sample_json() {
    return st::dump_json(st::card_to_json(sample_card()));
}

std::string sample_png() {
    const std::string encoded = st::base64_encode(sample_json());
    return png_fixture::build_png({{"chara", encoded}, {"ccv3", encoded}});
}

std::uint32_t read_be32(const std::string& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 3]));
}

std::vector<std::size_t> chunk_offsets(const std::string& png) {
    std::vector<std::size_t> offsets;
    std::size_t offset = 8;
    while (offset + 8 <= png.size()) {
        offsets.push_back(offset);
        const std::uint32_t length = read_be32(png, offset);
        offset += 12 + static_cast<std::size_t>(length);
    }
    return offsets;
}

std::uint64_t xorshift(std::uint64_t& state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

bool parse_throws_runtime_error(const std::string& bytes, bool& other_exception) {
    try {
        (void)st::parse_character_card(bytes, "property");
    } catch (const std::runtime_error&) {
        return true;
    } catch (...) {
        other_exception = true;
    }
    return false;
}

}  // namespace

TEST(card_property_json_truncations_throw) {
    const std::string json = sample_json();
    const auto full = st::parse_character_card(json, "card");
    CHECK_EQ(full.name, "Seraphina");

    for (std::size_t length = 0; length < json.size(); ++length) {
        bool other_exception = false;
        const bool threw = parse_throws_runtime_error(json.substr(0, length), other_exception);
        CHECK(threw);
        CHECK(!other_exception);
    }
}

TEST(card_property_png_truncations_throw) {
    const std::string png = sample_png();
    const auto full = st::parse_character_card(png, "png");
    CHECK_EQ(full.name, "Seraphina");

    for (std::size_t length = 0; length < png.size(); ++length) {
        bool other_exception = false;
        const bool threw = parse_throws_runtime_error(png.substr(0, length), other_exception);
        CHECK(threw);
        CHECK(!other_exception);
    }
}

TEST(card_property_png_mutations_are_contained) {
    const std::string png = sample_png();
    const std::vector<std::size_t> offsets = chunk_offsets(png);
    CHECK(!offsets.empty());

    std::uint64_t state = 0xC0FFEE123456789ULL;
    for (int iteration = 0; iteration < 2000; ++iteration) {
        std::string mutated = png;
        const std::uint64_t mode = xorshift(state) % 3;

        if (mode == 0) {
            const std::size_t position = static_cast<std::size_t>(xorshift(state) % mutated.size());
            const unsigned int bit = static_cast<unsigned int>(xorshift(state) % 8);
            mutated[position] = static_cast<char>(
                static_cast<unsigned char>(mutated[position]) ^ (1u << bit));
        } else if (mode == 1 && !offsets.empty()) {
            const std::size_t offset = offsets[static_cast<std::size_t>(xorshift(state) % offsets.size())];
            for (std::size_t i = 0; i < 4; ++i) {
                mutated[offset + i] = static_cast<char>(0xFF);
            }
        } else {
            const std::size_t length = static_cast<std::size_t>(xorshift(state) % (mutated.size() + 1));
            mutated.resize(length);
        }

        bool other_exception = false;
        (void)parse_throws_runtime_error(mutated, other_exception);
        CHECK(!other_exception);
    }
}
