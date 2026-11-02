#include "test_util.h"
#include "png_fixture.h"
#include "temp_dir.h"

#include "core/atomic_file.h"
#include "core/base64.h"
#include "core/character_store.h"

#include <filesystem>
#include <string>

namespace {

std::string card_json(const std::string& name) {
    return R"({"spec":"chara_card_v2","data":{"name":")" + name +
           R"(","description":"A test card."}})";
}

std::string card_png(const std::string& name) {
    return png_fixture::build_png({{"chara", st::base64_encode(card_json(name))}});
}

}  // namespace

TEST(store_imports_png_and_lists) {
    TempDir temp;
    st::CharacterStore store(temp.path() / "characters");
    const auto source = temp.path() / "source.png";
    st::write_file_atomic(source, card_png("Seraphina"));

    const auto entry = store.import_file(source);
    CHECK_EQ(entry.card.name, "Seraphina");
    CHECK_EQ(entry.id, "Seraphina");
    CHECK(std::filesystem::exists(entry.path));
    CHECK_EQ(entry.path.parent_path(), temp.path() / "characters");
    CHECK_EQ(store.list().size(), std::size_t(1));
    CHECK_EQ(store.avatar_path(entry), entry.path);
}

TEST(store_import_collision_gets_suffix) {
    TempDir temp;
    st::CharacterStore store(temp.path() / "characters");
    const auto source = temp.path() / "source.json";
    st::write_file_atomic(source, card_json("Same"));

    const auto first = store.import_file(source);
    const auto second = store.import_file(source);
    CHECK_EQ(first.id, "Same");
    CHECK_EQ(second.id, "Same-2");
    CHECK(std::filesystem::exists(first.path));
    CHECK(std::filesystem::exists(second.path));
}

TEST(store_rejects_invalid_before_copy) {
    TempDir temp;
    st::CharacterStore store(temp.path() / "characters");
    const auto source = temp.path() / "bad.png";
    st::write_file_atomic(source, "not a card");

    bool threw = false;
    try {
        store.import_file(source);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(store.list().empty());
    CHECK(!std::filesystem::exists(temp.path() / "characters" / "bad.png"));
}

TEST(store_import_rejects_oversized_card) {
    TempDir temp;
    st::CharacterStore store(temp.path() / "characters");
    const auto source = temp.path() / "huge.png";
    st::write_file_atomic(source, "");
    std::filesystem::resize_file(source, 64u * 1024u * 1024u + 1u);

    bool threw = false;
    try {
        (void)store.import_file(source);
    } catch (const std::runtime_error& error) {
        threw = true;
        CHECK(std::string(error.what()).find("too large") != std::string::npos);
    }
    CHECK(threw);
    CHECK(store.list().empty());
}

TEST(store_remove_deletes_only_id) {
    TempDir temp;
    st::CharacterStore store(temp.path() / "characters");
    const auto source = temp.path() / "source.json";
    st::write_file_atomic(source, card_json("Gone"));
    const auto entry = store.import_file(source);

    store.remove(entry.id);
    CHECK(store.list().empty());
    CHECK(!std::filesystem::exists(entry.path));
}

TEST(store_json_card_has_no_avatar) {
    TempDir temp;
    st::CharacterStore store(temp.path() / "characters");
    const auto source = temp.path() / "source.json";
    st::write_file_atomic(source, card_json("JsonOnly"));
    const auto entry = store.import_file(source);
    CHECK(store.avatar_path(entry).empty());
}

TEST(store_import_sanitizes_name) {
    TempDir temp;
    st::CharacterStore store(temp.path() / "characters");
    const auto source = temp.path() / "source.json";
    st::write_file_atomic(source, card_json("../Evil/Name"));
    const auto entry = store.import_file(source);
    CHECK_EQ(entry.id, "Evil_Name");
    CHECK_EQ(entry.path.parent_path(), temp.path() / "characters");
    CHECK(std::filesystem::exists(entry.path));
}

TEST(store_list_sorted_by_name) {
    TempDir temp;
    st::CharacterStore store(temp.path() / "characters");
    const auto zeta = temp.path() / "zeta.json";
    const auto alpha = temp.path() / "alpha.json";
    st::write_file_atomic(zeta, card_json("Zeta"));
    st::write_file_atomic(alpha, card_json("Alpha"));
    store.import_file(zeta);
    store.import_file(alpha);

    const auto entries = store.list();
    CHECK_EQ(entries.size(), std::size_t(2));
    CHECK_EQ(entries[0].card.name, "Alpha");
    CHECK_EQ(entries[1].card.name, "Zeta");
}

TEST(store_import_does_not_modify_source) {
    TempDir temp;
    st::CharacterStore store(temp.path() / "characters");
    const auto source = temp.path() / "source.json";
    const std::string original = card_json("Original");
    st::write_file_atomic(source, original);
    store.import_file(source);
    CHECK_EQ(st::read_file(source), original);
}
