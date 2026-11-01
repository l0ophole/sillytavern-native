# Stage 1 — Core Platform Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and test the GTK-free platform layer: XDG paths, atomic files, JSON, logging, settings, credentials, HTTP, and SSE.

**Architecture:** Add `src/core/` and a static `libstcore.a`. Tests link the library without GTK. Every module has one responsibility and a narrow header. libcurl is the only network dependency; nlohmann/json is vendored.

**Tech Stack:** C++17, POSIX file APIs, std::filesystem, libcurl, nlohmann/json, GNU Make.

**Spec:** `docs/superpowers/specs/2026-11-01-sillytavern-cpp-port-design.md`
**Master plan:** `docs/plans/00-v0.1-master-plan.md`

## Global Constraints

- `src/core/` must not include GTK headers.
- All writes are atomic and stay under the supplied root.
- Settings and credential files use mode 0600.
- Logs redact API keys.
- Tests use temporary directories and never touch real XDG data.
- One feature commit per module, immediately after its tests pass.
- `make test` must run every test file without a display server.

## Review Focus

1. A write interrupted before rename must leave the previous file intact.
2. A settings file with wrong JSON types must not crash; it must fall back or throw a clear error.
3. An API key must never appear in a log line or error string.
4. SSE chunks split mid-line and mid-UTF-8 must reassemble exactly.
5. An HTTP timeout or TLS failure must return a clear error, not a hang or a crash.

---

### Task 1: Add the core library and P02 paths/atomic I/O

**Files:**
- Create: `src/core/paths.h`, `src/core/paths.cpp`
- Create: `src/core/atomic_file.h`, `src/core/atomic_file.cpp`
- Create: `tests/temp_dir.h`, `tests/test_paths.cpp`, `tests/test_atomic_file.cpp`
- Modify: `Makefile`

**Interfaces:**
- `st::Paths::resolve()` and `st::Paths::resolve(config_home, data_home)` return directories:
  `config_dir`, `data_dir`, `characters_dir`, `chats_dir`, `logs_dir`, `settings_file`.
- `Paths::ensure()` creates config/data/characters/chats/logs.
- `st::read_file(path) -> std::string` throws `std::runtime_error` if unreadable.
- `st::write_file_atomic(path, data, perms = 0644)` creates parents, writes a sibling temp file, fsyncs, renames, and throws on failure.
- `st::file_exists(path) -> bool`.

- [ ] **Step 1: Write failing path tests**

`test_paths.cpp`:

```cpp
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
    CHECK(std::filesystem::is_directory(p.characters_dir));
    CHECK(std::filesystem::is_directory(p.chats_dir));
    CHECK(std::filesystem::is_directory(p.logs_dir));
}
```

- [ ] **Step 2: Write failing atomic-file tests**

`test_atomic_file.cpp`:

```cpp
TEST(atomic_write_read_roundtrip) {
    TempDir temp;
    const auto file = temp.path() / "nested" / "file.txt";
    st::write_file_atomic(file, "hello");
    CHECK_EQ(st::read_file(file), "hello");
}

TEST(atomic_write_replaces_existing) {
    TempDir temp;
    const auto file = temp.path() / "file.txt";
    st::write_file_atomic(file, "old");
    st::write_file_atomic(file, "new");
    CHECK_EQ(st::read_file(file), "new");
}

TEST(atomic_write_sets_requested_mode) {
    TempDir temp;
    const auto file = temp.path() / "secret.json";
    st::write_file_atomic(file, "{}", std::filesystem::perms::owner_read |
                                      std::filesystem::perms::owner_write);
    struct stat info {};
    CHECK_EQ(::stat(file.c_str(), &info), 0);
    CHECK_EQ(info.st_mode & 0777, 0600);
}

TEST(read_missing_file_throws) {
    TempDir temp;
    bool threw = false;
    try { st::read_file(temp.path() / "missing"); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
}
```

`tests/temp_dir.h` provides a non-copyable RAII directory under
`std::filesystem::temp_directory_path()` named with pid + counter, and removes
it in the destructor.

- [ ] **Step 3: Run the tests and verify they fail**

Run: `make test`
Expected: compile failure because `core/paths.h` and `core/atomic_file.h` do not exist.

- [ ] **Step 4: Implement paths and atomic files**

`paths.cpp` uses `XDG_CONFIG_HOME` or `$HOME/.config`, `XDG_DATA_HOME` or
`$HOME/.local/share`. If `HOME` is unset, use `.config` and `.local/share`
relative to the current directory. Append `sillytavern-port`.

`atomic_file.cpp` uses POSIX `open`, `write`, `fsync`, `close`, `rename`, and
`chmod`; converts `std::filesystem::perms` to a mode; cleans the temp file on
error. Temp names are `<filename>.tmp.<pid>.<counter>`.

- [ ] **Step 5: Update the Makefile for libstcore and all tests**

Add:

```make
CORE_SRCS := $(wildcard src/core/*.cpp)
CORE_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(CORE_SRCS))
CORE_LIB := $(BUILD)/libstcore.a
TEST_SRCS := $(wildcard tests/test_*.cpp)
TEST_OBJS := $(patsubst tests/%.cpp,$(BUILD)/tests/%.o,$(TEST_SRCS))
CURL_LIBS := $(shell pkg-config --libs libcurl 2>/dev/null || echo -lcurl)
CORE_LIBS := $(CURL_LIBS) -pthread
```

Rules compile `src/core/%.cpp` to `build/core/%.o`, archive `libstcore.a`, and
link `build/run_tests` from all `TEST_OBJS`. Add `-include $(wildcard $(BUILD)/**/*.d)`.
`make test` must run every `TEST` registered by any `tests/test_*.cpp`.

- [ ] **Step 6: Run the tests and verify they pass**

Run: `make test`
Expected: all path and atomic-file tests PASS.

- [ ] **Step 7: Commit P02**

```bash
git add src/core/paths.* src/core/atomic_file.* tests/temp_dir.h tests/test_paths.cpp tests/test_atomic_file.cpp Makefile
git commit -m "feat(core): add XDG paths and atomic file I/O"
```

---

### Task 2: P03 JSON layer

**Files:**
- Create: `src/core/json.h`, `src/core/json.cpp`
- Test: `tests/test_json.cpp`

**Interfaces:**
- `st::Json` aliases `nlohmann::json`.
- `st::parse_json(text, context) -> Json` throws `std::runtime_error` containing `context` on parse failure.
- `st::dump_json(value, indent = -1) -> std::string`.

- [ ] **Step 1: Write failing tests**

```cpp
TEST(json_parse_object) {
    const auto value = st::parse_json(R"({"name":"Seraphina","age":2})", "card");
    CHECK_EQ(value["name"].get<std::string>(), "Seraphina");
    CHECK_EQ(value["age"].get<int>(), 2);
}

TEST(json_parse_error_mentions_context) {
    bool threw = false;
    try { st::parse_json("{", "character card"); }
    catch (const std::runtime_error& error) {
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
```

- [ ] **Step 2: Run and verify failure**

Run: `make test`
Expected: compile failure for `core/json.h`.

- [ ] **Step 3: Implement the JSON helpers**

Catch `nlohmann::json::parse_error` and `nlohmann::json::type_error`; rethrow
as `std::runtime_error(context + ": " + error.what())`. `dump_json` uses
`value.dump(indent)`.

- [ ] **Step 4: Run and verify pass**

Run: `make test`
Expected: JSON tests PASS.

- [ ] **Step 5: Commit P03**

```bash
git add src/core/json.* tests/test_json.cpp
git commit -m "feat(core): add JSON serialization layer"
```

---

### Task 3: P04 logging and redaction

**Files:**
- Create: `src/core/log.h`, `src/core/log.cpp`
- Test: `tests/test_log.cpp`

**Interfaces:**
- `st::LogLevel { Debug, Info, Warn, Error }`
- `st::log_init(path, bool also_stderr = true)`, `st::log_shutdown()`
- `st::log_write(LogLevel, message)`
- `st::log_set_redactions(std::vector<std::string>)`
- `st::redact_secrets(text, secrets) -> std::string`

- [ ] **Step 1: Write failing tests**

```cpp
TEST(redact_replaces_every_secret) {
    const auto text = st::redact_secrets("key=sk-abc other=sk-abc", {"sk-abc"});
    CHECK_EQ(text, "key=[REDACTED] other=[REDACTED]");
}

TEST(redact_ignores_empty_secret) {
    CHECK_EQ(st::redact_secrets("plain", {""}), "plain");
}

TEST(log_writes_to_file_and_redacts) {
    TempDir temp;
    const auto file = temp.path() / "app.log";
    st::log_init(file, false);
    st::log_set_redactions({"sk-secret"});
    st::log_write(st::LogLevel::Info, "api key sk-secret");
    st::log_shutdown();
    const auto contents = st::read_file(file);
    CHECK(contents.find("api key") != std::string::npos);
    CHECK(contents.find("sk-secret") == std::string::npos);
    CHECK(contents.find("[REDACTED]") != std::string::npos);
}
```

- [ ] **Step 2: Run and verify failure**

Run: `make test`
Expected: compile failure for `core/log.h`.

- [ ] **Step 3: Implement thread-safe logging**

Use a mutex, an optional `std::ofstream`, a timestamp in ISO-8601 local time,
level tags `[DEBUG] [INFO] [WARN] [ERROR]`, and the redaction list. `log_write`
also writes to `std::cerr` when enabled. `log_shutdown` flushes and closes.

- [ ] **Step 4: Run and verify pass**

Run: `make test`
Expected: logging tests PASS.

- [ ] **Step 5: Commit P04**

```bash
git add src/core/log.* tests/test_log.cpp
git commit -m "feat(core): add logging and diagnostics"
```

---

### Task 4: P05 settings

**Files:**
- Create: `src/core/settings.h`, `src/core/settings.cpp`
- Test: `tests/test_settings.cpp`

**Interfaces:**
- `st::Settings` fields exactly as the spec section 5.4, plus `Json unknown`.
- `Settings::load(path) -> Settings` returns defaults when the file is absent.
- `Settings::save(path) const` writes 0600 JSON atomically.
- `Settings::validate()` clamps values.
- `Settings::to_json()` / `Settings::from_json(Json)` preserve unknown keys.

- [ ] **Step 1: Write failing tests**

```cpp
TEST(settings_defaults_when_missing) {
    TempDir temp;
    const auto settings = st::Settings::load(temp.path() / "settings.json");
    CHECK_EQ(settings.model, "gpt-4o-mini");
    CHECK_EQ(settings.max_tokens, 300);
    CHECK_EQ(settings.context_size, 4095);
    CHECK_EQ(settings.user_name, "User");
    CHECK(settings.stream);
}

TEST(settings_roundtrip_preserves_unknown_keys) {
    TempDir temp;
    auto settings = st::Settings::load(temp.path() / "settings.json");
    settings.api_key = "sk-test";
    settings.unknown["future_flag"] = true;
    settings.save(temp.path() / "settings.json");
    const auto loaded = st::Settings::load(temp.path() / "settings.json");
    CHECK_EQ(loaded.api_key, "sk-test");
    CHECK_EQ(loaded.unknown["future_flag"].get<bool>(), true);
}

TEST(settings_validate_clamps) {
    st::Settings settings;
    settings.temperature = 9.0;
    settings.top_p = -1.0;
    settings.max_tokens = 999999;
    settings.context_size = 128;
    settings.request_timeout_seconds = 1;
    settings.validate();
    CHECK_EQ(settings.temperature, 2.0);
    CHECK_EQ(settings.top_p, 0.0);
    CHECK_EQ(settings.context_size, 256);
    CHECK(settings.max_tokens >= 1);
    CHECK(settings.max_tokens < settings.context_size);
    CHECK_EQ(settings.request_timeout_seconds, 5);
}

TEST(settings_save_is_private) {
    TempDir temp;
    const auto file = temp.path() / "settings.json";
    st::Settings{}.save(file);
    struct stat info {};
    CHECK_EQ(::stat(file.c_str(), &info), 0);
    CHECK_EQ(info.st_mode & 0777, 0600);
}
```

- [ ] **Step 2: Run and verify failure**

Run: `make test`
Expected: compile failure for `core/settings.h`.

- [ ] **Step 3: Implement settings**

Use `st::Json`, `st::parse_json`, `st::dump_json`, and
`st::write_file_atomic(..., 0600)`. `from_json` reads each known field only when
the type is correct; otherwise keep the default and record the key in `unknown`.
`validate` implements the exact clamps in the tests.

- [ ] **Step 4: Run and verify pass**

Run: `make test`
Expected: settings tests PASS.

- [ ] **Step 5: Commit P05**

```bash
git add src/core/settings.* tests/test_settings.cpp
git commit -m "feat(core): add settings and preferences"
```

---

### Task 5: P06 credential helpers

**Files:**
- Create: `src/core/credentials.h`, `src/core/credentials.cpp`
- Test: `tests/test_credentials.cpp`

**Interfaces:**
- `st::ensure_private_file(path)` sets mode 0600 and throws on failure.
- `st::mask_secret(secret) -> std::string`: empty stays empty; length <= 8 becomes `"****"`; longer becomes first 3 + `"..."` + last 4.
- `st::is_secret_redacted(text, secret) -> bool` convenience for tests/log checks.

- [ ] **Step 1: Write failing tests**

```cpp
TEST(mask_secret_hides_middle) {
    CHECK_EQ(st::mask_secret(""), "");
    CHECK_EQ(st::mask_secret("short"), "****");
    CHECK_EQ(st::mask_secret("sk-1234567890"), "sk-...7890");
}

TEST(ensure_private_file_sets_0600) {
    TempDir temp;
    const auto file = temp.path() / "secret";
    st::write_file_atomic(file, "x", std::filesystem::perms::owner_read |
                                   std::filesystem::perms::group_read |
                                   std::filesystem::perms::others_read);
    st::ensure_private_file(file);
    struct stat info {};
    CHECK_EQ(::stat(file.c_str(), &info), 0);
    CHECK_EQ(info.st_mode & 0777, 0600);
}
```

- [ ] **Step 2: Run and verify failure**

Run: `make test`
Expected: compile failure for `core/credentials.h`.

- [ ] **Step 3: Implement credentials**

Use `::chmod(path.c_str(), 0600)`. `mask_secret` follows the exact lengths above.

- [ ] **Step 4: Run and verify pass**

Run: `make test`
Expected: credential tests PASS.

- [ ] **Step 5: Commit P06**

```bash
git add src/core/credentials.* tests/test_credentials.cpp
git commit -m "feat(core): add credential storage helpers"
```

---

### Task 6: P07 SSE parser and HTTP client

**Files:**
- Create: `src/core/sse.h`, `src/core/sse.cpp`
- Create: `src/core/http_client.h`, `src/core/http_client.cpp`
- Test: `tests/test_sse.cpp`, `tests/test_http_client.cpp`

**Interfaces:**
- `st::SseEvent { std::string event; std::string data; }`
- `st::SseParser::feed(const char*, size_t) -> std::vector<SseEvent>`
- `st::SseParser::feed(const std::string&)`, `finish()`, `reset()`.
- `st::HttpRequest { url, headers, body, timeout_seconds=300, connect_timeout_seconds=15, follow_redirects=true }`
- `st::HttpResponse { status, body, error, cancelled }`
- `st::HttpClient::post(request, on_data = {}) -> HttpResponse`; `cancel()`.

- [ ] **Step 1: Write failing SSE tests**

```cpp
TEST(sse_parses_split_chunks) {
    st::SseParser parser;
    std::vector<st::SseEvent> events;
    const std::string first = "data: hel";
    const std::string second = "lo\n\n";
    for (const auto& event : parser.feed(first)) events.push_back(event);
    for (const auto& event : parser.feed(second)) events.push_back(event);
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].data, "hello");
}

TEST(sse_parses_multiline_data_and_crlf) {
    st::SseParser parser;
    const auto events = parser.feed("event: message\r\ndata: a\r\ndata: b\r\n\r\n");
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].event, "message");
    CHECK_EQ(events[0].data, "a\nb");
}

TEST(sse_ignores_comments_and_handles_done) {
    st::SseParser parser;
    const auto events = parser.feed(": keepalive\ndata: [DONE]\n\n");
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].data, "[DONE]");
}

TEST(sse_finish_dispatches_pending_event) {
    st::SseParser parser;
    CHECK_EQ(parser.feed("data: tail").size(), std::size_t(0));
    const auto events = parser.finish();
    CHECK_EQ(events.size(), std::size_t(1));
    CHECK_EQ(events[0].data, "tail");
}
```

- [ ] **Step 2: Run and verify failure**

Run: `make test`
Expected: compile failure for `core/sse.h`.

- [ ] **Step 3: Implement the SSE parser**

Buffer bytes, split on `\n`, strip a trailing `\r`. Blank line dispatches when
data is present. `data:` appends after one optional leading space. `event:`
sets the event name. Comments (`:`) are ignored. `finish()` processes any
remaining buffered line and dispatches a pending event.

- [ ] **Step 4: Write failing HTTP tests**

```cpp
TEST(http_rejects_invalid_url) {
    st::HttpClient client;
    st::HttpRequest request;
    request.url = "not-a-url";
    request.body = "{}";
    const auto response = client.post(request);
    CHECK(!response.error.empty());
    CHECK_EQ(response.status, 0L);
}

TEST(http_streams_data_callback) {
    // Skipped unless ST_MOCK_BASE_URL is set by integration_openai.sh.
    const char* base = std::getenv("ST_MOCK_BASE_URL");
    if (base == nullptr) return;
    st::HttpClient client;
    st::HttpRequest request;
    request.url = std::string(base) + "/chat/completions";
    request.headers = {"Content-Type: application/json"};
    request.body = R"({"stream":true})";
    std::string received;
    const auto response = client.post(request, [&](const char* data, std::size_t size) {
        received.append(data, size);
        return true;
    });
    CHECK_EQ(response.status, 200L);
    CHECK(received.find("data:") != std::string::npos);
}
```

- [ ] **Step 5: Run and verify failure**

Run: `make test`
Expected: compile failure for `core/http_client.h`.

- [ ] **Step 6: Implement the HTTP client**

Use `curl_global_init` once. Set URL, POST, headers, body, timeouts,
`CURLOPT_NOSIGNAL`, `CURLOPT_FOLLOWLOCATION`, a write callback that appends to
`response.body` and calls `on_data`, and an xferinfo callback that checks
`cancelled_`. Return `error` from the curl error buffer on failure. `cancel()`
sets the atomic flag; the transfer stops at the next progress callback.

- [ ] **Step 7: Run and verify pass**

Run: `make test`
Expected: SSE tests PASS; invalid-URL test PASS; streaming test is skipped
without `ST_MOCK_BASE_URL`.

- [ ] **Step 8: Commit P07**

```bash
git add src/core/sse.* src/core/http_client.* tests/test_sse.cpp tests/test_http_client.cpp
git commit -m "feat(core): add HTTP/TLS and SSE transport"
```

---

## Stage 1 completion checklist

- [ ] All seven modules compile with `-Wall -Wextra -Wpedantic` and no warnings.
- [ ] `make test` passes all unit tests.
- [ ] `src/core/` contains no GTK includes.
- [ ] `make` still builds the skeleton app.
- [ ] P02–P07 statuses are `ported` in `features.json`.
- [ ] `docs/PORT_LOG.md` has rows for P02–P07 with commit hashes.
- [ ] `docs/FEATURES.md` regenerated.
- [ ] `git status` is clean.
