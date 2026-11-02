# Stage 3 — Prompting, Budgeting, Sampling, and Streaming Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn stored cards and chats into an OpenAI-compatible request, trim it to a token budget, stream the reply back, and expose the user's identity and basic sampling controls.

**Architecture:** Five GTK-free core modules: `identity` (persistent user name normalization), `prompt` (macro expansion and message construction), `context_budget` (heuristic token estimate and history trimming), `sampling` (validated generation parameters), and `openai_client` (URL/request/SSE/error handling on top of `http_client`). Tests use a Python mock server for transport behavior; all unit tests run without a display or network.

**Tech Stack:** C++17, libcurl, nlohmann/json, Python 3 for the mock server.

**Spec:** `docs/superpowers/specs/2026-11-01-sillytavern-cpp-port-design.md`
**Master plan:** `docs/plans/00-v0.1-master-plan.md`
**Previous stage:** `docs/plans/03-stage-2-data-compatibility.md`

## Global Constraints

- `src/core/` must not include GTK headers.
- Prompt text is built from card fields in the exact order in spec section 6.
- `{{char}}`, `<BOT>`, `{{user}}`, and `<USER>` are replaced case-insensitively.
- The token estimate is deterministic: `ceil(UTF-8 code points / 4)` plus 4 tokens per message.
- Streaming preserves partial text on error or cancellation.
- API keys never appear in errors or logs.
- One feature commit per completed feature, immediately after tests pass.
- `make test` must stay warning-free under `-Wall -Wextra -Wpedantic`.

## Review Focus

1. Prompt order and macro expansion must match the spec; an empty optional field must not add a blank section.
2. Trimming must keep message order, keep the greeting when possible, and never exceed the budget.
3. A stream split at arbitrary byte boundaries must reassemble exactly.
4. HTTP errors, malformed SSE, and cancellation must surface a clear error or partial text, never hang.
5. Sampling values must be clamped before they reach the request JSON.

---

### Task 1: C30 persistent user identity

**Files:**
- Create: `src/core/identity.h`, `src/core/identity.cpp`
- Test: `tests/test_identity.cpp`

**Interfaces:**
- `std::string st::normalize_user_name(const std::string& name);`
  - removes ASCII control characters;
  - trims leading/trailing whitespace;
  - collapses internal whitespace runs to one space;
  - returns `"User"` when empty;
  - caps the result at 64 UTF-8 bytes without splitting a sequence.
- `Settings::validate()` uses `normalize_user_name` so every loaded or saved settings object has a valid identity.

- [ ] **Step 1: Write failing tests**

```cpp
TEST(identity_defaults_to_user) {
    CHECK_EQ(st::normalize_user_name(""), "User");
    CHECK_EQ(st::normalize_user_name("   "), "User");
}

TEST(identity_trims_and_collapses) {
    CHECK_EQ(st::normalize_user_name("  Alice  "), "Alice");
    CHECK_EQ(st::normalize_user_name("Alice   Smith"), "Alice Smith");
}

TEST(identity_removes_controls) {
    CHECK_EQ(st::normalize_user_name(std::string("A") + '\x01' + "B"), "AB");
}

TEST(identity_caps_length) {
    CHECK(st::normalize_user_name(std::string(200, 'a')).size() <= 64);
    std::string multibyte;
    for (int i = 0; i < 100; ++i) multibyte += "é";
    const auto result = st::normalize_user_name(multibyte);
    CHECK(result.size() <= 64);
    CHECK_EQ(result.size() % 2, std::size_t(0));
}
```

- [ ] **Step 2: Run and verify failure**

Run: `cd SillyTavern_port && make test`
Expected: compile failure for `core/identity.h`.

- [ ] **Step 3: Implement identity and wire settings**

Implement the normalization in `identity.cpp`. In `settings.cpp`, replace the inline trim/fallback for `user_name` with `normalize_user_name`, and add `#include "core/identity.h"`.

- [ ] **Step 4: Run and verify pass**

Run: `make test`
Expected: identity tests PASS and existing settings tests still PASS.

- [ ] **Step 5: Commit C30**

```bash
git add src/core/identity.* src/core/settings.cpp tests/test_identity.cpp \
        SillyTavern_port/features/features.json docs/FEATURES.md docs/PORT_LOG.md
git commit -m "feat(chat): add persistent user identity"
```

Set C30 to `ported`, regenerate the feature doc, and add the log row.

---

### Task 2: C03 prompt assembly and core macros

**Files:**
- Create: `src/core/prompt.h`, `src/core/prompt.cpp`
- Test: `tests/test_prompt.cpp`

**Interfaces:**
- `struct st::PromptMessage { std::string role; std::string content; };`
- `std::string st::expand_macros(const std::string& text, const std::string& character, const std::string& user);`
  - case-insensitive replacement of `{{char}}`, `<BOT>`, `{{user}}`, `<USER>`;
  - no recursive expansion.
- `std::vector<PromptMessage> st::build_prompt(const CharacterCard& card, const Settings& settings, const std::vector<ChatMessage>& history);`
  - one system message containing, in order: card system prompt or global system prompt, description, `"<char>'s personality: <personality>"`, `"Scenario: <scenario>"`, `"Example dialogues:\n<mes_example>"`;
  - history messages: `is_system` → `system`, `is_user` → `user`, otherwise `assistant`; empty `mes` skipped;
  - post-history instructions appended as the final `system` message;
  - macros expanded in every content string.

- [ ] **Step 1: Write failing tests**

```cpp
TEST(prompt_expands_core_macros) {
    CHECK_EQ(st::expand_macros("{{char}} and <USER>", "Seraphina", "Alice"),
             "Seraphina and Alice");
    CHECK_EQ(st::expand_macros("{{CHAR}} <bot> {{User}} <user>", "S", "A"), "S S A A");
    CHECK_EQ(st::expand_macros("no macros", "S", "A"), "no macros");
}

TEST(prompt_builds_system_sections_in_order) {
    st::CharacterCard card;
    card.name = "Seraphina";
    card.description = "A dragon.";
    card.personality = "Calm";
    card.scenario = "A cave";
    card.mes_example = "{{user}}: Hi\n{{char}}: Hello.";
    st::Settings settings;
    settings.user_name = "Alice";
    settings.system_prompt = "Global prompt.";

    const auto messages = st::build_prompt(card, settings, {});
    CHECK_EQ(messages.size(), std::size_t(1));
    CHECK_EQ(messages[0].role, "system");
    const std::string& content = messages[0].content;
    CHECK(content.find("Global prompt.") != std::string::npos);
    CHECK(content.find("A dragon.") != std::string::npos);
    CHECK(content.find("Seraphina's personality: Calm") != std::string::npos);
    CHECK(content.find("Scenario: A cave") != std::string::npos);
    CHECK(content.find("Example dialogues:\nAlice: Hi\nSeraphina: Hello.") != std::string::npos);
    CHECK(content.find("Global prompt.") < content.find("A dragon."));
    CHECK(content.find("A dragon.") < content.find("Scenario: A cave"));
}

TEST(prompt_prefers_character_system_prompt) {
    st::CharacterCard card;
    card.name = "Seraphina";
    card.system_prompt = "Character prompt.";
    st::Settings settings;
    settings.system_prompt = "Global prompt.";
    const auto messages = st::build_prompt(card, settings, {});
    CHECK(messages[0].content.find("Character prompt.") != std::string::npos);
    CHECK(messages[0].content.find("Global prompt.") == std::string::npos);
}

TEST(prompt_maps_history_roles_and_skips_empty) {
    st::CharacterCard card;
    card.name = "Seraphina";
    st::Settings settings;
    settings.user_name = "Alice";

    st::ChatMessage user;
    user.name = "Alice"; user.is_user = true; user.mes = "Hi {{char}}.";
    st::ChatMessage empty;
    empty.name = "Seraphina"; empty.mes = "";
    st::ChatMessage bot;
    bot.name = "Seraphina"; bot.mes = "Hello {{user}}.";
    st::ChatMessage system;
    system.name = "System"; system.is_system = true; system.mes = "Narrate.";

    const auto messages = st::build_prompt(card, settings, {user, empty, bot, system});
    CHECK_EQ(messages.size(), std::size_t(4));  // system prompt + 3 history
    CHECK_EQ(messages[1].role, "user");
    CHECK_EQ(messages[1].content, "Hi Seraphina.");
    CHECK_EQ(messages[2].role, "assistant");
    CHECK_EQ(messages[2].content, "Hello Alice.");
    CHECK_EQ(messages[3].role, "system");
    CHECK_EQ(messages[3].content, "Narrate.");
}

TEST(prompt_appends_post_history_instructions_last) {
    st::CharacterCard card;
    card.name = "Seraphina";
    card.post_history_instructions = "Be brief, {{user}}.";
    st::Settings settings;
    settings.user_name = "Alice";
    st::ChatMessage user;
    user.name = "Alice"; user.is_user = true; user.mes = "Hi";
    const auto messages = st::build_prompt(card, settings, {user});
    CHECK_EQ(messages.back().role, "system");
    CHECK_EQ(messages.back().content, "Be brief, Alice.");
}
```

- [ ] **Step 2: Run and verify failure**

Run: `make test`
Expected: compile failure for `core/prompt.h`.

- [ ] **Step 3: Implement prompt assembly**

Use a helper that appends a section with `"\n\n"` separators only when both sides are non-empty. Normalize the user name with `normalize_user_name`. Use `card.name` or `"Character"` when empty.

- [ ] **Step 4: Run and verify pass**

Run: `make test`
Expected: prompt tests PASS.

- [ ] **Step 5: Commit C03**

```bash
git add src/core/prompt.* tests/test_prompt.cpp \
        SillyTavern_port/features/features.json docs/FEATURES.md docs/PORT_LOG.md
git commit -m "feat(chat): add prompt assembly and core macros"
```

Set C03 to `ported`, regenerate the feature doc, and add the log row.

---

### Task 3: C06 context budgeting

**Files:**
- Create: `src/core/context_budget.h`, `src/core/context_budget.cpp`
- Test: `tests/test_context_budget.cpp`
- Modify: `SillyTavern_port/features/features.json` (C06 dependency correction)

**Interfaces:**
- `int st::estimate_tokens(const std::string& text);`
  - counts UTF-8 code points (bytes that are not `10xxxxxx`);
  - returns `ceil(code_points / 4)`, so empty text is 0.
- `int st::estimate_message_tokens(const PromptMessage& message);`
  - `estimate_tokens(content) + 4`.
- `int st::estimate_prompt_tokens(const std::vector<PromptMessage>& messages);`
- `struct st::BudgetResult { std::vector<PromptMessage> history; int estimated_tokens; bool trimmed; };`
- `BudgetResult st::fit_history(const std::vector<PromptMessage>& system_messages, const std::vector<PromptMessage>& history, int context_size, int max_tokens);`
  - budget = `max(0, context_size - max_tokens - estimate_prompt_tokens(system_messages))`;
  - keep the newest history messages that fit, preserving order;
  - when `history[0].role == "assistant"`, reserve it first if it fits;
  - stop adding once the next newest message does not fit;
  - `estimated_tokens` includes the system messages;
  - `trimmed` is true when any history message was dropped.

**Dependency correction:** C06 currently depends on C04, but trimming runs before transport and does not use the OpenAI client. Change C06 `depends_on` to `["C03", "P05"]`. Record this in the commit message.

- [ ] **Step 1: Write failing tests**

```cpp
TEST(budget_estimates_tokens) {
    CHECK_EQ(st::estimate_tokens(""), 0);
    CHECK_EQ(st::estimate_tokens("abcd"), 1);
    CHECK_EQ(st::estimate_tokens("abcde"), 2);
    CHECK_EQ(st::estimate_tokens("éééé"), 1);  // 4 code points, 8 bytes
    st::PromptMessage message{"user", "abcd"};
    CHECK_EQ(st::estimate_message_tokens(message), 5);
}

TEST(budget_keeps_all_when_it_fits) {
    const std::vector<st::PromptMessage> system{{"system", "short"}};
    const std::vector<st::PromptMessage> history{
        {"assistant", "hello"}, {"user", "hi"}, {"assistant", "there"}};
    const auto result = st::fit_history(system, history, 4096, 300);
    CHECK_EQ(result.history.size(), std::size_t(3));
    CHECK(!result.trimmed);
    CHECK(result.estimated_tokens <= 4096 - 300);
}

TEST(budget_trims_oldest_and_preserves_order) {
    const std::vector<st::PromptMessage> system{{"system", "s"}};
    std::vector<st::PromptMessage> history;
    for (int i = 0; i < 10; ++i) {
        history.push_back({"user", std::string(40, 'a')});
    }
    const auto result = st::fit_history(system, history, 80, 20);
    CHECK(result.trimmed);
    CHECK(result.history.size() < history.size());
    CHECK(!result.history.empty());
    CHECK(result.estimated_tokens <= 60);
    for (std::size_t i = 1; i < result.history.size(); ++i) {
        CHECK_EQ(result.history[i].content, history[history.size() - (result.history.size() - i)].content);
    }
}

TEST(budget_keeps_greeting_when_possible) {
    const std::vector<st::PromptMessage> system{{"system", "s"}};
    const std::vector<st::PromptMessage> history{
        {"assistant", "greeting"}, {"user", std::string(40, 'a')},
        {"assistant", std::string(40, 'b')}};
    const auto result = st::fit_history(system, history, 40, 10);
    CHECK(!result.history.empty());
    CHECK_EQ(result.history.front().role, "assistant");
    CHECK_EQ(result.history.front().content, "greeting");
    CHECK(result.estimated_tokens <= 30);
}

TEST(budget_drops_greeting_when_too_large) {
    const std::vector<st::PromptMessage> system{{"system", "s"}};
    const std::vector<st::PromptMessage> history{{"assistant", std::string(400, 'g')}};
    const auto result = st::fit_history(system, history, 40, 10);
    CHECK(result.history.empty());
    CHECK(result.trimmed);
}
```

- [ ] **Step 2: Run and verify failure**

Run: `make test`
Expected: compile failure for `core/context_budget.h`.

- [ ] **Step 3: Implement budgeting**

Implement the code-point count and the two-phase selection. Never mutate the input vectors.

- [ ] **Step 4: Run and verify pass**

Run: `make test`
Expected: budget tests PASS.

- [ ] **Step 5: Commit C06**

```bash
git add src/core/context_budget.* tests/test_context_budget.cpp \
        SillyTavern_port/features/features.json docs/FEATURES.md docs/PORT_LOG.md
git commit -m "feat(chat): add context budgeting

C06 depends on C03/P05 only; trimming does not use the OpenAI client."
```

Set C06 to `ported`, regenerate the feature doc, and add the log row.

---

### Task 4: C39 basic sampling controls

**Files:**
- Create: `src/core/sampling.h`, `src/core/sampling.cpp`
- Test: `tests/test_sampling.cpp`

**Interfaces:**
- `struct st::Sampling { double temperature = 1.0; double top_p = 1.0; int max_tokens = 300; bool stream = true; };`
- `Sampling st::clamp_sampling(Sampling value);`
  - temperature → `[0, 2]`; non-finite → 1.0;
  - top_p → `[0, 1]`; non-finite → 1.0;
  - max_tokens → `[1, 1'000'000]`;
  - stream unchanged.
- `Sampling st::sampling_from_settings(const Settings& settings);`
- `void st::apply_sampling(Settings& settings, const Sampling& sampling);`
- `Json st::sampling_json(const Sampling& sampling);` → `temperature`, `top_p`, `max_tokens`, `stream`.

- [ ] **Step 1: Write failing tests**

```cpp
TEST(sampling_defaults_match_settings) {
    const st::Settings settings;
    const auto sampling = st::sampling_from_settings(settings);
    CHECK_EQ(sampling.temperature, 1.0);
    CHECK_EQ(sampling.top_p, 1.0);
    CHECK_EQ(sampling.max_tokens, 300);
    CHECK(sampling.stream);
}

TEST(sampling_clamps_values) {
    st::Sampling sampling;
    sampling.temperature = 9.0;
    sampling.top_p = -1.0;
    sampling.max_tokens = 0;
    const auto clamped = st::clamp_sampling(sampling);
    CHECK_EQ(clamped.temperature, 2.0);
    CHECK_EQ(clamped.top_p, 0.0);
    CHECK_EQ(clamped.max_tokens, 1);
}

TEST(sampling_applies_to_settings) {
    st::Settings settings;
    st::Sampling sampling;
    sampling.temperature = 0.5;
    sampling.top_p = 0.9;
    sampling.max_tokens = 128;
    sampling.stream = false;
    st::apply_sampling(settings, sampling);
    CHECK_EQ(settings.temperature, 0.5);
    CHECK_EQ(settings.top_p, 0.9);
    CHECK_EQ(settings.max_tokens, 128);
    CHECK(!settings.stream);
}

TEST(sampling_json_has_request_fields) {
    st::Sampling sampling;
    const auto value = st::sampling_json(sampling);
    CHECK_EQ(value["temperature"].get<double>(), 1.0);
    CHECK_EQ(value["top_p"].get<double>(), 1.0);
    CHECK_EQ(value["max_tokens"].get<int>(), 300);
    CHECK_EQ(value["stream"].get<bool>(), true);
}
```

- [ ] **Step 2: Run and verify failure**

Run: `make test`
Expected: compile failure for `core/sampling.h`.

- [ ] **Step 3: Implement sampling**

Reuse the same clamps as `Settings::validate()`; include `<cmath>` for `std::isfinite`.

- [ ] **Step 4: Run and verify pass**

Run: `make test`
Expected: sampling tests PASS.

- [ ] **Step 5: Commit C39**

```bash
git add src/core/sampling.* tests/test_sampling.cpp \
        SillyTavern_port/features/features.json docs/FEATURES.md docs/PORT_LOG.md
git commit -m "feat(chat): add basic sampling controls"
```

Set C39 to `ported`, regenerate the feature doc, and add the log row.

---

### Task 5: C04 OpenAI-compatible streaming client

**Files:**
- Create: `src/core/openai_client.h`, `src/core/openai_client.cpp`
- Create: `tests/mock_openai.py`, `tests/integration_openai.sh`
- Test: `tests/test_openai_client.cpp`, `tests/test_openai_integration.cpp`

**Interfaces:**
- `std::string st::openai_chat_url(const std::string& base_url);`
  - trims trailing `/`;
  - returns unchanged when it already ends in `/chat/completions`;
  - otherwise appends `/chat/completions`.
- `Json st::openai_request_json(const OpenAiRequest& request);`
  - `model`, `messages` (`role`/`content`), and all `sampling_json` fields.
- `std::string st::openai_error_message(long status, const std::string& body);`
  - prefers `error.message`, then `detail`, then `HTTP <status>`.
- `struct st::OpenAiRequest { std::string base_url, api_key, model; std::vector<PromptMessage> messages; Sampling sampling; int timeout_seconds = 300; };`
- `struct st::OpenAiResult { std::string text, error; long status = 0; bool cancelled = false; };`
- `using OpenAiChunkCallback = std::function<void(const std::string&)>;`
- `class st::OpenAiClient { OpenAiResult generate(const OpenAiRequest&, const OpenAiChunkCallback& = {}); void cancel(); };`
  - builds the HTTP request with `Content-Type: application/json`, `Accept: text/event-stream` when streaming, and `Authorization: Bearer <key>` only when the key is non-empty;
  - feeds response bytes through `SseParser`, appends `choices[0].delta.content`, and calls the chunk callback;
  - stops at `data: [DONE]`;
  - accepts a non-streaming `choices[0].message.content` JSON response;
  - keeps partial text on error or cancellation;
  - HTTP status ≥ 400 sets `error` from `openai_error_message`;
  - cancellation forwards to `HttpClient::cancel()` and sets `cancelled`.

- [ ] **Step 1: Write failing unit tests**

```cpp
TEST(openai_chat_url_variants) {
    CHECK_EQ(st::openai_chat_url("https://api.openai.com/v1"),
             "https://api.openai.com/v1/chat/completions");
    CHECK_EQ(st::openai_chat_url("https://api.openai.com/v1/"),
             "https://api.openai.com/v1/chat/completions");
    CHECK_EQ(st::openai_chat_url("http://localhost:1234/chat/completions"),
             "http://localhost:1234/chat/completions");
    CHECK_EQ(st::openai_chat_url("http://localhost:1234"),
             "http://localhost:1234/chat/completions");
}

TEST(openai_request_json_has_fields) {
    st::OpenAiRequest request;
    request.model = "gpt-4o-mini";
    request.messages = {{"system", "Be nice."}, {"user", "Hi"}};
    request.sampling.temperature = 0.7;
    request.sampling.max_tokens = 64;
    const auto value = st::openai_request_json(request);
    CHECK_EQ(value["model"].get<std::string>(), "gpt-4o-mini");
    CHECK_EQ(value["messages"].size(), std::size_t(2));
    CHECK_EQ(value["messages"][0]["role"].get<std::string>(), "system");
    CHECK_EQ(value["messages"][1]["content"].get<std::string>(), "Hi");
    CHECK_EQ(value["temperature"].get<double>(), 0.7);
    CHECK_EQ(value["max_tokens"].get<int>(), 64);
}

TEST(openai_error_message_variants) {
    CHECK_EQ(st::openai_error_message(401, R"({"error":{"message":"bad key"}})"), "bad key");
    CHECK_EQ(st::openai_error_message(400, R"({"detail":"bad request"})"), "bad request");
    CHECK_EQ(st::openai_error_message(500, "oops"), "HTTP 500");
}
```

- [ ] **Step 2: Run and verify failure**

Run: `make test`
Expected: compile failure for `core/openai_client.h`.

- [ ] **Step 3: Implement URL, request, and error helpers**

Use `st::parse_json` and `st::dump_json`. Do not include the API key in any error string.

- [ ] **Step 4: Implement generation**

Use `HttpClient::post` with a write callback that feeds `SseParser::feed`. Track a `done` flag. On each event:
- `[DONE]` → set done and return true;
- otherwise parse JSON; if `choices[0].delta.content` is a string, append it and call `on_chunk`.

After the transfer, if the body is non-empty and no streamed text was produced, try a non-streaming parse of `choices[0].message.content`. Map status/errors as specified. On `CURLE_ABORTED_BY_CALLBACK` with cancellation, return partial text and `cancelled = true`.

- [ ] **Step 5: Add the mock server and integration tests**

`tests/mock_openai.py` binds `127.0.0.1:0`, writes the port to the path given as `argv[1]`, and serves:

- `POST /chat/completions`: reads the JSON body; if `stream` is true, sends
  `data: {"choices":[{"delta":{"content":"hello"}}]}\n\n`,
  `data: {"choices":[{"delta":{"content":" world"}}]}\n\n`,
  `data: [DONE]\n\n` in two separate `wfile.write` calls; otherwise sends
  `{"choices":[{"message":{"content":"hello"}}]}` as `application/json`.
- `POST /slow/chat/completions`: sleeps 2 seconds, then streams one chunk and `[DONE]`.
- `POST /error/chat/completions`: returns 401 with `{"error":{"message":"bad key"}}`.

`tests/integration_openai.sh` starts the server, waits for the port file, exports
`ST_MOCK_BASE_URL`, runs `build/run_tests`, and kills the server with a trap.

`tests/test_openai_integration.cpp`:

```cpp
TEST(openai_streams_from_mock_server) {
    const char* base = std::getenv("ST_MOCK_BASE_URL");
    if (base == nullptr) return;
    st::OpenAiClient client;
    st::OpenAiRequest request;
    request.base_url = base;
    request.model = "mock";
    request.messages = {{"user", "Hi"}};
    std::string chunks;
    const auto result = client.generate(request, [&](const std::string& chunk) { chunks += chunk; });
    CHECK(result.error.empty());
    CHECK_EQ(result.status, 200L);
    CHECK_EQ(result.text, "hello world");
    CHECK_EQ(chunks, "hello world");
}

TEST(openai_reports_mock_error) {
    const char* base = std::getenv("ST_MOCK_BASE_URL");
    if (base == nullptr) return;
    st::OpenAiClient client;
    st::OpenAiRequest request;
    request.base_url = std::string(base) + "/error";
    request.model = "mock";
    request.messages = {{"user", "Hi"}};
    const auto result = client.generate(request);
    CHECK_EQ(result.status, 401L);
    CHECK_EQ(result.error, "bad key");
}

TEST(openai_cancels_slow_stream) {
    const char* base = std::getenv("ST_MOCK_BASE_URL");
    if (base == nullptr) return;
    st::OpenAiClient client;
    st::OpenAiRequest request;
    request.base_url = std::string(base) + "/slow";
    request.model = "mock";
    request.messages = {{"user", "Hi"}};
    std::thread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        client.cancel();
    });
    const auto result = client.generate(request);
    canceller.join();
    CHECK(result.cancelled);
}
```

Include `<thread>` and `<chrono>` in the integration test.

- [ ] **Step 6: Run unit tests and the integration script**

```bash
make test
./tests/integration_openai.sh
```

Expected: all unit tests PASS; the integration script prints the same test
count with all tests PASS.

- [ ] **Step 7: Commit C04**

```bash
git add src/core/openai_client.* tests/test_openai_client.cpp \
        tests/test_openai_integration.cpp tests/mock_openai.py \
        tests/integration_openai.sh \
        SillyTavern_port/features/features.json docs/FEATURES.md docs/PORT_LOG.md
git commit -m "feat(chat): add OpenAI-compatible streaming client"
```

Set C04 to `ported`, regenerate the feature doc, and add the log row.

---

### Task 6: Stage 3 verification and wrap-up

- [ ] **Step 1: Run the full gate**

```bash
cd SillyTavern_port
make clean
make
make test
./tests/integration_openai.sh
./build/menuconfig --selftest
```

Expected: build and unit tests pass with no warnings; integration tests pass;
selftest passes.

- [ ] **Step 2: Verify no GTK and correct feature status**

```bash
grep -rIn 'gtk\|gtkmm' src/core && exit 1 || true
grep -n 'CONFIG_C30\|CONFIG_C03\|CONFIG_C06\|CONFIG_C39\|CONFIG_C04' .config
```

Expected: all five are `=y`; no GTK includes.

- [ ] **Step 3: Update the log and commit**

Record the real hashes and the Stage 3 row, then:

```bash
git add docs/PORT_LOG.md docs/FEATURES.md
git commit -m "docs: record stage 3 completion"
```

---

## Stage 3 completion checklist

- [ ] C30, C03, C06, C39, C04 are `ported`.
- [ ] Prompt order, macros, role mapping, and post-history instructions are tested.
- [ ] Trimming keeps order, keeps the greeting when possible, and respects the budget.
- [ ] Sampling is clamped before reaching the request.
- [ ] Streaming, non-streaming, HTTP errors, and cancellation are tested against the mock server.
- [ ] `make test` and `./tests/integration_openai.sh` pass with no warnings.
- [ ] `docs/FEATURES.md` regenerated; `docs/PORT_LOG.md` updated.
- [ ] `git status` is clean.
