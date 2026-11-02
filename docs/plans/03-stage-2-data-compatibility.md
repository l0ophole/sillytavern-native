# Stage 2 — Data Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Read and write SillyTavern character cards and chat files safely, with filename sanitization and path containment.

**Architecture:** Four GTK-free core modules: `names` (sanitization and path safety), `base64` + `character_card` (PNG tEXt and JSON cards), `character_store` (library scan/import/delete), and `chat_store` (ST-compatible JSONL). Every write is atomic and confined to the configured data root. D02's core is completed here; its avatar UI lands in stage 4, so D02 stays `planned` until then. C01's dependency on D02 is removed because chat storage needs only a character name and first message, not the character library module.

**Tech Stack:** C++17, POSIX file APIs, std::filesystem, vendored nlohmann/json, GNU Make.

**Spec:** `docs/superpowers/specs/2026-11-01-sillytavern-cpp-port-design.md`
**Master plan:** `docs/plans/00-v0.1-master-plan.md`
**Previous stage:** `docs/plans/02-stage-1-core-platform.md`

## Global Constraints

- `src/core/` must not include GTK headers.
- Every file write is atomic and stays under the supplied data root.
- Import copies the source file; the source is never modified.
- Character-card and chat formats must match SillyTavern 1.19.0.
- Unknown JSON fields are preserved on load/save.
- Tests use `TempDir` and never touch real XDG data.
- One feature commit per completed feature, immediately after tests pass.
- `make test` must stay warning-free under `-Wall -Wextra -Wpedantic`.

## Review Focus

1. A card name or chat filename containing `../`, absolute paths, control characters, or a collision must never escape the data root or overwrite another file.
2. A PNG containing both `ccv3` and `chara` must use `ccv3`; malformed chunks and bad base64 must produce clear errors, never a crash.
3. A chat JSONL round-trip must preserve message order, roles, timestamps, metadata, and unknown fields.
4. A failed import must not leave a partial file in the library.
5. A malformed chat line must report the file and line number.

---

### Task 1: O01 filename sanitization and path safety

**Files:**
- Create: `src/core/names.h`, `src/core/names.cpp`
- Test: `tests/test_names.cpp`

**Interfaces:**
- `std::string st::sanitize_filename(const std::string& name, const std::string& fallback = "unnamed");`
  - replaces `/` and `\` with `_`;
  - removes ASCII control characters;
  - trims leading/trailing spaces and dots;
  - collapses consecutive `_` runs;
  - returns `fallback` when the result is empty or is `.`/`..`.
- `std::filesystem::path st::safe_join(const std::filesystem::path& root, const std::filesystem::path& relative);`
  - rejects absolute `relative` and any result that is not lexically inside `root`;
  - returns `root / relative` normalized;
  - throws `std::runtime_error` on escape.

- [ ] **Step 1: Write failing tests**

```cpp
TEST(sanitize_rejects_traversal) {
    CHECK_EQ(st::sanitize_filename("../etc/passwd"), "etc_passwd");
    CHECK_EQ(st::sanitize_filename(".."), "unnamed");
    CHECK_EQ(st::sanitize_filename("/absolute"), "absolute");
}

TEST(sanitize_removes_control_characters) {
    CHECK_EQ(st::sanitize_filename(std::string("a\x01b\tc", 5)), "abc");
    CHECK_EQ(st::sanitize_filename("  spaced name  "), "spaced name");
}

TEST(sanitize_preserves_unicode) {
    CHECK_EQ(st::sanitize_filename("Séra—夜"), "Séra—夜");
}

TEST(sanitize_uses_fallback_for_empty) {
    CHECK_EQ(st::sanitize_filename(""), "unnamed");
    CHECK_EQ(st::sanitize_filename("...", "fallback"), "fallback");
}

TEST(safe_join_confines_paths) {
    TempDir temp;
    const auto joined = st::safe_join(temp.path(), "chats/Seraphina");
    CHECK_EQ(joined, temp.path() / "chats" / "Seraphina");
    bool threw = false;
    try { st::safe_join(temp.path(), "../escape"); }
    catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    bool absolute_threw = false;
    try { st::safe_join(temp.path(), "/etc/passwd"); }
    catch (const std::runtime_error&) { absolute_threw = true; }
    CHECK(absolute_threw);
}
```

- [ ] **Step 2: Run and verify failure**

Run: `cd SillyTavern_port && make test`
Expected: compile failure for `core/names.h`.

- [ ] **Step 3: Implement names**

Implement `sanitize_filename` by iterating bytes, keeping UTF-8 bytes >= 0x20 except `/` and `\`, replacing them with `_`, then trimming and collapsing. Implement `safe_join` with `lexically_normal`, rejecting absolute inputs and results whose first components differ from `root`. Use `std::filesystem::path` comparison after normalization.

- [ ] **Step 4: Run and verify pass**

Run: `make test`
Expected: names tests PASS.

- [ ] **Step 5: Commit O01**

```bash
git add src/core/names.h src/core/names.cpp tests/test_names.cpp
git commit -m "feat(core): add filename sanitization and path safety"
```

Update `features.json` O01 to `ported`, regenerate `docs/FEATURES.md`, add the `docs/PORT_LOG.md` row, and include those files in the same commit.

---

### Task 2: D01 base64 and character-card parser

**Files:**
- Create: `src/core/base64.h`, `src/core/base64.cpp`
- Create: `src/core/character_card.h`, `src/core/character_card.cpp`
- Test: `tests/test_base64.cpp`, `tests/test_character_card.cpp`

**Interfaces:**
- `std::string st::base64_decode(const std::string& text, const std::string& context);`
  - strict: rejects non-alphabet characters, invalid length, and bad padding;
  - accepts standard `+`/`/` alphabet and trailing `=` padding.
- `std::string st::base64_encode(const std::string& bytes);`
- `struct st::CharacterCard` fields:
  `name`, `description`, `personality`, `scenario`, `first_mes`, `mes_example`,
  `system_prompt`, `post_history_instructions`, `creator`, `character_version`,
  `spec_version` (all `std::string`), `alternate_greetings` (`std::vector<std::string>`),
  `tags` (`std::vector<std::string>`), `Json extra` (unknown card fields).
- `CharacterCard st::parse_character_card(const std::string& bytes, const std::string& context);`
  - detects the 8-byte PNG signature; otherwise treats the input as UTF-8 JSON;
  - PNG: walks chunks, reads `tEXt` entries, prefers keyword `ccv3`, else `chara`;
  - decodes the selected value as base64 and parses the resulting JSON card.
- `CharacterCard st::parse_character_card_file(const std::filesystem::path& path);`
- `Json st::card_to_json(const CharacterCard& card);`
- `CharacterCard st::card_from_json(const Json& value);`
  - V2/V3: when `spec` starts with `chara_card_v` and `data` is an object, read from `data`;
  - legacy: read fields from the root;
  - `name` is required and must be non-empty.

- [ ] **Step 1: Write failing base64 tests**

```cpp
TEST(base64_roundtrip) {
    CHECK_EQ(st::base64_encode("hello"), "aGVsbG8=");
    CHECK_EQ(st::base64_decode("aGVsbG8=", "test"), "hello");
}

TEST(base64_rejects_invalid_input) {
    for (const char* bad : {"a", "aGVsbG8", "aGVsbG8!", "===="}) {
        bool threw = false;
        try { st::base64_decode(bad, "test"); }
        catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
    }
}

TEST(base64_handles_binary) {
    const std::string bytes("\x00\x01\xff", 3);
    CHECK_EQ(st::base64_decode(st::base64_encode(bytes), "test"), bytes);
}
```

- [ ] **Step 2: Run and verify failure**

Run: `make test`
Expected: compile failure for `core/base64.h`.

- [ ] **Step 3: Implement base64**

Use a 256-entry decode table initialized to -1. Validate length % 4 == 0, padding only at the end, and unused low bits zero. Throw `runtime_error(context + ": invalid base64")` on failure.

- [ ] **Step 4: Write failing card tests**

Add a test helper `tests/png_fixture.h` that builds a synthetic PNG from tEXt pairs:

```cpp
std::string build_png(const std::vector<std::pair<std::string, std::string>>& texts);
```

Tests:

```cpp
TEST(card_parses_v2_json) {
    const auto card = st::parse_character_card(R"({
        "spec":"chara_card_v2","spec_version":"2.0",
        "data":{"name":"Seraphina","description":"A dragon.","first_mes":"Hello.",
                "alternate_greetings":["Hi."],"tags":["fantasy"],"future":{"x":1}}
    })", "card");
    CHECK_EQ(card.name, "Seraphina");
    CHECK_EQ(card.first_mes, "Hello.");
    CHECK_EQ(card.alternate_greetings.size(), std::size_t(1));
    CHECK_EQ(card.extra["future"]["x"].get<int>(), 1);
}

TEST(card_parses_legacy_json) {
    const auto card = st::parse_character_card(R"({"name":"Old","description":"Legacy"})", "card");
    CHECK_EQ(card.name, "Old");
    CHECK_EQ(card.description, "Legacy");
}

TEST(card_png_prefers_ccv3_over_chara) {
    const auto png = build_png({
        {"chara", st::base64_encode(R"({"name":"Old"})")},
        {"ccv3", st::base64_encode(R"({"spec":"chara_card_v3","data":{"name":"New"}})")},
    });
    CHECK_EQ(st::parse_character_card(png, "png").name, "New");
}

TEST(card_rejects_missing_name) {
    bool threw = false;
    try { st::parse_character_card(R"({"description":"x"})", "card"); }
    catch (const std::runtime_error& error) {
        threw = true;
        CHECK(std::string(error.what()).find("name") != std::string::npos);
    }
    CHECK(threw);
}

TEST(card_rejects_malformed_png) {
    bool threw = false;
    try { st::parse_character_card(std::string("\x89PNG\r\n\x1a\nbad", 12), "png"); }
    catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
}
```

- [ ] **Step 5: Implement the parser**

PNG walk: verify signature, then loop reading big-endian length, 4-byte type, data, and skip 4-byte CRC. Reject lengths beyond the buffer. For `tEXt`, split at the first NUL into keyword and text. Choose `ccv3` over `chara`. If neither exists, throw `"no character card metadata found"`. Cap chunk length and total input size at 64 MiB to avoid memory abuse.

- [ ] **Step 6: Run and verify pass**

Run: `make test`
Expected: base64 and card tests PASS.

- [ ] **Step 7: Commit D01**

```bash
git add src/core/base64.* src/core/character_card.* tests/test_base64.cpp tests/test_character_card.cpp tests/png_fixture.h
git commit -m "feat(characters): add character card parser"
```

Update D01 status/log/docs in the same commit.

---

### Task 3: D02 character library core

**Files:**
- Create: `src/core/character_store.h`, `src/core/character_store.cpp`
- Test: `tests/test_character_store.cpp`

**Interfaces:**
- `struct st::CharacterEntry { std::string id; std::filesystem::path path; CharacterCard card; };`
- `st::CharacterStore(std::filesystem::path characters_dir);`
- `std::vector<CharacterEntry> list() const;` sorted by `card.name`, then `id`.
- `CharacterEntry import_file(const std::filesystem::path& source);`
  - parses the source first; invalid cards are rejected before any copy;
  - target name is `sanitize_filename(card.name)` plus the source extension (`.png` or `.json`);
  - collisions get `-2`, `-3`, ... before the extension;
  - copies atomically via `write_file_atomic` with the source bytes;
  - returns the stored entry.
- `void remove(const std::string& id);` deletes only files whose stem matches `id` inside the characters directory.
- `std::filesystem::path avatar_path(const CharacterEntry& entry) const;`
  - returns the card path when its extension is `.png`; returns an empty path for JSON cards.

- [ ] **Step 1: Write failing tests**

Use `TempDir`, `build_png`, and a small JSON card fixture.

```cpp
TEST(store_imports_png_and_lists) {
    TempDir temp;
    st::CharacterStore store(temp.path() / "characters");
    const auto source = temp.path() / "source.png";
    st::write_file_atomic(source, build_png({{"chara", st::base64_encode(card_json("Seraphina"))}}));
    const auto entry = store.import_file(source);
    CHECK_EQ(entry.card.name, "Seraphina");
    CHECK_EQ(entry.id, "Seraphina");
    CHECK(std::filesystem::exists(entry.path));
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
    try { store.import_file(source); }
    catch (const std::runtime_error&) { threw = true; }
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
```

- [ ] **Step 2: Run and verify failure**

Run: `make test`
Expected: compile failure for `core/character_store.h`.

- [ ] **Step 3: Implement the store**

Create the directory on demand. `list()` scans regular files with `.png` or `.json`, parses each, and skips files that fail to parse (log a warning if the log module is initialized; do not throw). `import_file` reads once, parses once, chooses a free filename, and writes atomically. `remove` sanitizes the id and uses `safe_join`.

- [ ] **Step 4: Run and verify pass**

Run: `make test`
Expected: store tests PASS.

- [ ] **Step 5: Commit the D02 core portion**

```bash
git add src/core/character_store.* tests/test_character_store.cpp
git commit -m "feat(characters): add character library and import core"
```

D02 stays `planned`; add a `docs/PORT_LOG.md` row marked `partial` with the note "core complete; avatar UI in stage 4". Do not regenerate the feature doc count for D02.

---

### Task 4: C01 chat storage and lifecycle

**Files:**
- Create: `src/core/chat_types.h`
- Create: `src/core/chat_store.h`, `src/core/chat_store.cpp`
- Test: `tests/test_chat_store.cpp`
- Modify: `SillyTavern_port/features/features.json` (C01 dependency list)

**Interfaces:**
- `struct st::ChatMessage`:
  `std::string name`, `bool is_user = false`, `bool is_system = false`,
  `std::string send_date`, `std::string mes`, `Json extra = Json::object();`
- `struct st::ChatMetadata`:
  `std::string user_name`, `std::string character_name`, `std::string create_date`,
  `Json chat_metadata = Json::object();`, `Json extra = Json::object();`
- `struct st::Chat { ChatMetadata metadata; std::vector<ChatMessage> messages; };`
- `st::ChatStore(std::filesystem::path chats_root);`
- `std::filesystem::path chat_dir(const std::string& character) const;`
  uses `sanitize_filename` and `safe_join`.
- `std::vector<std::string> list(const std::string& character) const;`
  returns `.jsonl` filenames sorted ascending.
- `Chat load(const std::string& character, const std::string& filename) const;`
- `void save(const std::string& character, const std::string& filename, const Chat& chat) const;`
- `Chat create(const std::string& character, const std::string& user_name,
              const std::string& first_mes) const;`
  sets ISO-8601 UTC timestamps and appends the greeting when `first_mes` is non-empty.
- `void rename(const std::string& character, const std::string& from,
              const std::string& to) const;`
- `void remove(const std::string& character, const std::string& filename) const;`

**JSONL format:** line 1 is the metadata object with `user_name`,
`character_name`, `create_date`, `chat_metadata`, plus preserved unknown fields.
Each later non-empty line is a message object with `name`, `is_user`, `is_system`,
`send_date`, `mes`, `extra`, plus preserved unknown fields. Saves use compact
JSON, one object per line, and end with a newline.

**Dependency correction:** C01 currently depends on D02, but chat storage needs
only a character name and first message. Change C01 `depends_on` to
`["P02", "P03"]` so C01 can be marked `ported` while D02's UI is still pending.
Record this in the plan and commit message.

- [ ] **Step 1: Write failing tests**

```cpp
TEST(chat_create_includes_greeting) {
    TempDir temp;
    st::ChatStore store(temp.path());
    const auto chat = store.create("Seraphina", "User", "Hello there.");
    CHECK_EQ(chat.metadata.character_name, "Seraphina");
    CHECK_EQ(chat.metadata.user_name, "User");
    CHECK_EQ(chat.messages.size(), std::size_t(1));
    CHECK_EQ(chat.messages[0].name, "Seraphina");
    CHECK(!chat.messages[0].is_user);
    CHECK_EQ(chat.messages[0].mes, "Hello there.");
    CHECK(!chat.metadata.create_date.empty());
    CHECK(!chat.messages[0].send_date.empty());
}

TEST(chat_roundtrip_preserves_order_and_unknown_fields) {
    TempDir temp;
    st::ChatStore store(temp.path());
    st::Chat chat;
    chat.metadata.user_name = "User";
    chat.metadata.character_name = "Seraphina";
    chat.metadata.create_date = "2026-11-01T00:00:00Z";
    chat.metadata.chat_metadata["scenario"] = "cave";
    chat.metadata.extra["future_meta"] = true;
    st::ChatMessage user;
    user.name = "User"; user.is_user = true;
    user.send_date = "2026-11-01T00:00:01Z"; user.mes = "Hi";
    user.extra["future_msg"] = 42;
    st::ChatMessage bot;
    bot.name = "Seraphina"; bot.is_user = false;
    bot.send_date = "2026-11-01T00:00:02Z"; bot.mes = "Hello";
    chat.messages = {user, bot};

    store.save("Seraphina", "chat.jsonl", chat);
    const auto loaded = store.load("Seraphina", "chat.jsonl");
    CHECK_EQ(loaded.metadata.user_name, "User");
    CHECK_EQ(loaded.metadata.chat_metadata["scenario"].get<std::string>(), "cave");
    CHECK_EQ(loaded.metadata.extra["future_meta"].get<bool>(), true);
    CHECK_EQ(loaded.messages.size(), std::size_t(2));
    CHECK(loaded.messages[0].is_user);
    CHECK_EQ(loaded.messages[0].mes, "Hi");
    CHECK_EQ(loaded.messages[0].extra["future_msg"].get<int>(), 42);
    CHECK_EQ(loaded.messages[1].name, "Seraphina");
    CHECK(!loaded.messages[1].is_user);
}

TEST(chat_list_rename_remove) {
    TempDir temp;
    st::ChatStore store(temp.path());
    const auto chat = store.create("Seraphina", "User", "Hi");
    store.save("Seraphina", "a.jsonl", chat);
    store.save("Seraphina", "b.jsonl", chat);
    CHECK_EQ(store.list("Seraphina").size(), std::size_t(2));
    store.rename("Seraphina", "a.jsonl", "c.jsonl");
    const auto names = store.list("Seraphina");
    CHECK_EQ(names[0], "b.jsonl");
    CHECK_EQ(names[1], "c.jsonl");
    store.remove("Seraphina", "b.jsonl");
    CHECK_EQ(store.list("Seraphina").size(), std::size_t(1));
}

TEST(chat_malformed_line_reports_location) {
    TempDir temp;
    st::ChatStore store(temp.path());
    const auto dir = store.chat_dir("Seraphina");
    st::write_file_atomic(dir / "bad.jsonl",
        R"({"user_name":"User","character_name":"Seraphina","create_date":"x"})" "\n"
        "{not json}\n");
    bool threw = false;
    try { store.load("Seraphina", "bad.jsonl"); }
    catch (const std::runtime_error& error) {
        threw = true;
        const std::string message = error.what();
        CHECK(message.find("bad.jsonl") != std::string::npos);
        CHECK(message.find("2") != std::string::npos);
    }
    CHECK(threw);
}

TEST(chat_rejects_path_escape) {
    TempDir temp;
    st::ChatStore store(temp.path());
    bool threw = false;
    try { store.load("../../etc", "passwd"); }
    catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
}
```

- [ ] **Step 2: Run and verify failure**

Run: `make test`
Expected: compile failure for `core/chat_store.h`.

- [ ] **Step 3: Implement the chat store**

Use `st::Json`, `st::parse_json`, `st::dump_json`, `st::read_file`, and
`st::write_file_atomic`. For metadata, copy the raw object into `extra`, then
erase known keys. For each message, do the same with `extra`. Parse the first
line as metadata; every subsequent non-empty line must be a message object.
Report errors as `"<filename>: line <n>: <message>"`. Use UTC ISO-8601
timestamps (`%Y-%m-%dT%H:%M:%SZ`).

- [ ] **Step 4: Run and verify pass**

Run: `make test`
Expected: chat tests PASS.

- [ ] **Step 5: Commit C01**

```bash
git add src/core/chat_types.h src/core/chat_store.* tests/test_chat_store.cpp \
        SillyTavern_port/features/features.json docs/FEATURES.md docs/PORT_LOG.md
git commit -m "feat(chat): add chat session storage

C01 depends on P02/P03 only; chat storage does not require the character
library module. D02 remains planned until its avatar UI lands."
```

Update C01 status to `ported`, regenerate `docs/FEATURES.md`, and add the log row.

---

### Task 5: Stage 2 verification and wrap-up

**Files:**
- Modify: `docs/PORT_LOG.md` (stage table, D02 partial note)
- Regenerate: `docs/FEATURES.md`

- [ ] **Step 1: Run the full verification**

```bash
cd SillyTavern_port
make clean
make
make test
./build/menuconfig --selftest
```

Expected: build and tests pass with no warnings; selftest passes.

- [ ] **Step 2: Verify containment and no GTK**

```bash
grep -rIn 'gtk\|gtkmm' src/core && exit 1 || true
grep -n 'C01' .config
```

Expected: no GTK includes; `CONFIG_C01=y` after defaults regeneration.

- [ ] **Step 3: Update the stage table and commit**

Record Stage 2 as complete with test counts and the mock/containment evidence,
then:

```bash
git add docs/PORT_LOG.md docs/FEATURES.md
git commit -m "docs: record stage 2 completion"
```

---

## Stage 2 completion checklist

- [ ] O01, D01, C01 are `ported`; D02 is `planned` with a `partial` log row.
- [ ] `make` and `make test` pass with no warnings.
- [ ] A PNG card with both `ccv3` and `chara` selects `ccv3`.
- [ ] Path traversal is rejected by both sanitization and `safe_join`.
- [ ] Import never modifies the source and never leaves partial files.
- [ ] Chat round-trip preserves order, roles, timestamps, and unknown fields.
- [ ] `docs/FEATURES.md` regenerated; `docs/PORT_LOG.md` updated.
- [ ] `git status` is clean.
