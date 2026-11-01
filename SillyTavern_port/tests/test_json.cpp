#include "test_util.h"

#include "core/json.h"

#include <stdexcept>
#include <string>

TEST(json_parse_object) {
    const auto value = st::parse_json(R"({"name":"Seraphina","age":2})", "card");
    CHECK_EQ(value["name"].get<std::string>(), "Seraphina");
    CHECK_EQ(value["age"].get<int>(), 2);
}

TEST(json_parse_error_mentions_context) {
    bool threw = false;
    try {
        st::parse_json("{", "character card");
    } catch (const std::runtime_error& error) {
        threw = true;
        CHECK(std::string(error.what()).find("character card") != std::string::npos);
    }
    CHECK(threw);
}

TEST(json_roundtrip_preserves_unknown_fields) {
    const auto value = st::parse_json(R"({"known":1,"future":{"x":true}})", "test");
    const auto again = st::parse_json(st::dump_json(value), "test");
    CHECK_EQ(again["future"]["x"].get<bool>(), true);
}
