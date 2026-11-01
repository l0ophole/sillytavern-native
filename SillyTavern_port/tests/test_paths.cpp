#include "test_util.h"
#include "temp_dir.h"

#include "core/paths.h"

TEST(paths_resolve_explicit) {
    const auto p = st::Paths::resolve("/tmp/cfg", "/tmp/data");
    CHECK_EQ(p.config_dir, std::filesystem::path("/tmp/cfg/sillytavern-port"));
    CHECK_EQ(p.data_dir, std::filesystem::path("/tmp/data/sillytavern-port"));
    CHECK_EQ(p.characters_dir, p.data_dir / "characters");
    CHECK_EQ(p.chats_dir, p.data_dir / "chats");
    CHECK_EQ(p.logs_dir, p.data_dir / "logs");
    CHECK_EQ(p.settings_file, p.config_dir / "settings.json");
}

TEST(paths_ensure_creates_directories) {
    TempDir temp;
    const auto p = st::Paths::resolve(temp.path() / "cfg", temp.path() / "data");
    p.ensure();
    CHECK(std::filesystem::is_directory(p.config_dir));
    CHECK(std::filesystem::is_directory(p.characters_dir));
    CHECK(std::filesystem::is_directory(p.chats_dir));
    CHECK(std::filesystem::is_directory(p.logs_dir));
}
