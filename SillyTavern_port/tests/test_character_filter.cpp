#include "test_util.h"

#include "core/character_filter.h"

#include <string>
#include <vector>

namespace {

st::CharacterEntry make_entry(const std::string& name,
                              const std::string& description,
                              const std::string& creator,
                              std::vector<std::string> tags) {
    st::CharacterEntry entry;
    entry.id = name;
    entry.card.name = name;
    entry.card.description = description;
    entry.card.creator = creator;
    entry.card.tags = std::move(tags);
    return entry;
}

std::vector<st::CharacterEntry> sample_entries() {
    return {
        make_entry("Seraphina", "A dragon.", "Tester", {"fantasy", "dragon"}),
        make_entry("Old Man", "Legacy wizard.", "Archive", {"classic"}),
        make_entry("Dragon", "A small dragon.", "Tester", {"fantasy"}),
    };
}

}  // namespace

TEST(filter_characters_empty_query_returns_all) {
    const auto entries = sample_entries();
    CHECK_EQ(st::filter_characters(entries, "").size(), std::size_t(3));
    CHECK_EQ(st::filter_characters(entries, "   ").size(), std::size_t(3));
}

TEST(filter_characters_matches_name_case_insensitively) {
    const auto entries = sample_entries();
    const auto result = st::filter_characters(entries, "sera");
    CHECK_EQ(result.size(), std::size_t(1));
    CHECK_EQ(result[0].card.name, "Seraphina");

    CHECK_EQ(st::filter_characters(entries, "DRAGON").size(), std::size_t(2));
}

TEST(filter_characters_matches_description_creator_and_tags) {
    const auto entries = sample_entries();
    const auto legacy = st::filter_characters(entries, "LEGACY");
    CHECK_EQ(legacy.size(), std::size_t(1));
    CHECK_EQ(legacy[0].card.name, "Old Man");

    const auto creator = st::filter_characters(entries, "archive");
    CHECK_EQ(creator.size(), std::size_t(1));

    const auto tag = st::filter_characters(entries, "fantasy");
    CHECK_EQ(tag.size(), std::size_t(2));
}

TEST(filter_characters_preserves_order) {
    const auto entries = sample_entries();
    const auto result = st::filter_characters(entries, "tester");
    CHECK_EQ(result.size(), std::size_t(2));
    CHECK_EQ(result[0].card.name, "Seraphina");
    CHECK_EQ(result[1].card.name, "Dragon");
}

TEST(filter_characters_returns_empty_for_no_match) {
    const auto entries = sample_entries();
    CHECK(st::filter_characters(entries, "zzz").empty());
}
