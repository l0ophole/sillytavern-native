#include "test_util.h"

#include "core/world_info.h"

#include <string>

TEST(world_info_trigger_case_insensitive) {
    st::WorldInfoBook book;
    book.entries = {{"dragon", "", "Dragons are rare.", true, 0, 0}};
    auto hits = st::triggered_entries(book, {"I saw a DRAGON"});
    CHECK_EQ(hits.size(), 1u);
}

TEST(world_info_trigger_comma_separated_keys) {
    st::WorldInfoBook book;
    book.entries = {{"dragon, wyrm", "", "Wyrm lore.", true, 0, 0}};
    auto hits = st::triggered_entries(book, {"A wyrm approaches"});
    CHECK_EQ(hits.size(), 1u);
    auto miss = st::triggered_entries(book, {"Nothing here"});
    CHECK(miss.empty());
}

TEST(world_info_disabled_skipped) {
    st::WorldInfoBook book;
    book.entries = {{"dragon", "", "Dragons are rare.", false, 0, 0}};
    auto hits = st::triggered_entries(book, {"I saw a dragon"});
    CHECK(hits.empty());
}

TEST(world_info_recursion_capped) {
    st::WorldInfoBook book;
    book.entries = {
        {"apple", "", "see banana", true, 0, 0},
        {"banana", "", "see apple", true, 0, 0},
    };
    auto hits = st::triggered_entries(book, {"apple"}, 5);
    CHECK(hits.size() <= 32u);
    CHECK_EQ(hits.size(), 2u);
}

TEST(world_info_ordering_and_dedup) {
    st::WorldInfoBook book;
    book.entries = {
        {"zebra", "", "Z lore.", true, 5, 0},
        {"alpha", "", "A lore.", true, 1, 0},
        {"alpha", "", "A lore.", true, 1, 0},
    };
    auto hits = st::triggered_entries(book, {"zebra alpha"});
    CHECK_EQ(hits.size(), 2u);
    CHECK_EQ(hits[0].key, "alpha");
    CHECK_EQ(hits[1].key, "zebra");
}

TEST(world_info_caps_entry_count_and_bytes) {
    st::WorldInfoBook book;
    for (int i = 0; i < 50; ++i) {
        st::WorldInfoEntry entry;
        entry.key = "key" + std::to_string(i) + " marker";
        entry.content = std::string(4096, 'x');
        entry.enabled = true;
        entry.position = i;
        book.entries.push_back(entry);
    }
    std::string text;
    for (int i = 0; i < 50; ++i) {
        text += " marker" + std::to_string(i);
    }
    // Include every key phrase explicitly.
    text = "marker marker";
    auto hits = st::triggered_entries(book, {text});
    CHECK(hits.size() <= 32u);
    std::size_t total = 0;
    for (const auto& hit : hits) {
        CHECK(hit.content.size() <= 4096u);
        total += hit.content.size();
    }
    CHECK(total <= 32768u);
}

TEST(world_info_truncates_long_content) {
    st::WorldInfoBook book;
    st::WorldInfoEntry entry;
    entry.key = "big";
    entry.content = std::string(9000, 'y');
    entry.enabled = true;
    book.entries = {entry};
    auto hits = st::triggered_entries(book, {"big"});
    CHECK_EQ(hits.size(), 1u);
    CHECK_EQ(hits[0].content.size(), 4096u);
}

TEST(world_info_from_json_tolerant) {
    const st::Json value = st::Json::parse(
        R"({"name":"Lore","entries":[{"key":"dragon","content":"Dragons","disable":false,"position":3,"comment":"hi","extra":1},{"content":"no key"}]})");
    const st::WorldInfoBook book = st::WorldInfoBook::from_json(value);
    CHECK_EQ(book.name, "Lore");
    CHECK_EQ(book.entries.size(), 1u);
    CHECK_EQ(book.entries[0].key, "dragon");
    CHECK(book.entries[0].enabled);
    CHECK_EQ(book.entries[0].position, 3);
    const st::Json round = book.to_json();
    CHECK(round.contains("entries"));
    CHECK_EQ(round["entries"].size(), 1u);
    CHECK_EQ(round["entries"][0]["disable"].get<bool>(), false);
}

TEST(world_info_from_json_bare_array) {
    const st::Json value =
        st::Json::parse(R"([{"key":"elves","content":"Elves","disable":true}])");
    const st::WorldInfoBook book = st::WorldInfoBook::from_json(value);
    CHECK_EQ(book.entries.size(), 1u);
    CHECK(!book.entries[0].enabled);
}
