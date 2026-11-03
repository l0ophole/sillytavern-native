#include "test_util.h"
#include "png_fixture.h"
#include "temp_dir.h"

#include "core/atomic_file.h"
#include "core/base64.h"
#include "core/character_card.h"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

std::string card_json(const std::string& name) {
    return R"({"spec":"chara_card_v2","spec_version":"2.0","data":{"name":")" + name +
           R"(","description":"A dragon.","first_mes":"Hello."}})";
}

}  // namespace

TEST(card_parses_v2_json) {
    const auto card = st::parse_character_card(R"({
        "spec":"chara_card_v2","spec_version":"2.0",
        "data":{"name":"Seraphina","description":"A dragon.","first_mes":"Hello.",
                "alternate_greetings":["Hi."],"tags":["fantasy"],"future":{"x":1}}
    })", "card");
    CHECK_EQ(card.name, "Seraphina");
    CHECK_EQ(card.description, "A dragon.");
    CHECK_EQ(card.first_mes, "Hello.");
    CHECK_EQ(card.alternate_greetings.size(), std::size_t(1));
    CHECK_EQ(card.alternate_greetings[0], "Hi.");
    CHECK_EQ(card.tags.size(), std::size_t(1));
    CHECK_EQ(card.tags[0], "fantasy");
    CHECK_EQ(card.spec_version, "2.0");
    CHECK_EQ(card.extra["future"]["x"].get<int>(), 1);
    CHECK(!card.extra.contains("name"));
}

TEST(card_parses_legacy_json) {
    const auto card = st::parse_character_card(R"({"name":"Old","description":"Legacy"})", "card");
    CHECK_EQ(card.name, "Old");
    CHECK_EQ(card.description, "Legacy");
    CHECK(card.spec_version.empty());
}

TEST(card_parses_v3_json) {
    const auto card = st::parse_character_card(
        R"({"spec":"chara_card_v3","data":{"name":"New","spec_version":"3.0"}})", "card");
    CHECK_EQ(card.name, "New");
    CHECK_EQ(card.spec_version, "3.0");
}

TEST(card_png_prefers_ccv3_over_chara) {
    const auto png = png_fixture::build_png({
        {"chara", st::base64_encode(R"({"name":"Old"})")},
        {"ccv3", st::base64_encode(R"({"spec":"chara_card_v3","data":{"name":"New"}})")},
    });
    CHECK_EQ(st::parse_character_card(png, "png").name, "New");
}

TEST(card_png_reads_chara_when_ccv3_missing) {
    const auto png = png_fixture::build_png({
        {"chara", st::base64_encode(card_json("OnlyChara"))},
    });
    CHECK_EQ(st::parse_character_card(png, "png").name, "OnlyChara");
}

TEST(card_rejects_missing_name) {
    bool threw = false;
    try {
        st::parse_character_card(R"({"description":"x"})", "card");
    } catch (const std::runtime_error& error) {
        threw = true;
        CHECK(std::string(error.what()).find("name") != std::string::npos);
    }
    CHECK(threw);
}

TEST(card_rejects_malformed_png) {
    bool threw = false;
    try {
        st::parse_character_card(std::string("\x89PNG\r\n\x1a\nbad", 12), "png");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(card_rejects_png_without_metadata) {
    const auto png = png_fixture::build_png({{"Comment", "not a card"}});
    bool threw = false;
    try {
        st::parse_character_card(png, "png");
    } catch (const std::runtime_error& error) {
        threw = true;
        CHECK(std::string(error.what()).find("metadata") != std::string::npos);
    }
    CHECK(threw);
}

TEST(card_rejects_invalid_base64_metadata) {
    const auto png = png_fixture::build_png({{"chara", "not base64!"}});
    bool threw = false;
    try {
        st::parse_character_card(png, "png");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(card_json_roundtrip) {
    st::CharacterCard original;
    original.name = "Seraphina";
    original.description = "A dragon.";
    original.personality = "Calm";
    original.scenario = "A cave";
    original.first_mes = "Hello.";
    original.mes_example = "{{user}}: Hi\n{{char}}: Hello.";
    original.system_prompt = "Stay in character.";
    original.post_history_instructions = "Be brief.";
    original.creator = "Tester";
    original.character_version = "1.2";
    original.spec_version = "2.0";
    original.alternate_greetings = {"Hi.", "Hey."};
    original.tags = {"fantasy"};
    original.extra["future"] = true;

    const auto restored = st::card_from_json(st::card_to_json(original));
    CHECK_EQ(restored.name, original.name);
    CHECK_EQ(restored.description, original.description);
    CHECK_EQ(restored.personality, original.personality);
    CHECK_EQ(restored.scenario, original.scenario);
    CHECK_EQ(restored.first_mes, original.first_mes);
    CHECK_EQ(restored.mes_example, original.mes_example);
    CHECK_EQ(restored.system_prompt, original.system_prompt);
    CHECK_EQ(restored.post_history_instructions, original.post_history_instructions);
    CHECK_EQ(restored.creator, original.creator);
    CHECK_EQ(restored.character_version, original.character_version);
    CHECK_EQ(restored.spec_version, original.spec_version);
    CHECK_EQ(restored.alternate_greetings.size(), std::size_t(2));
    CHECK_EQ(restored.tags.size(), std::size_t(1));
    CHECK_EQ(restored.extra["future"].get<bool>(), true);
}

TEST(card_file_rejects_oversized_card_before_reading) {
    TempDir temp;
    const auto path = temp.path() / "huge.png";
    st::write_file_atomic(path, "");
    std::filesystem::resize_file(path, 64u * 1024u * 1024u + 1u);

    bool threw = false;
    try {
        (void)st::parse_character_card_file(path);
    } catch (const std::runtime_error& error) {
        threw = true;
        CHECK(std::string(error.what()).find("too large") != std::string::npos);
    }
    CHECK(threw);
}
